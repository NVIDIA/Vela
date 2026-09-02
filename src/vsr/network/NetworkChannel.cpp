// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "NetworkChannel.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"
// std
#include <algorithm>
#include <cassert>

namespace vsr::network {

// Helper functions ///////////////////////////////////////////////////////////

template <typename FCN>
static void async_invoke(boost::asio::io_context &io_context, FCN &&f)
{
  boost::asio::post(io_context, [f = std::forward<FCN>(f)]() mutable { f(); });
}

// NetworkChannel definitions /////////////////////////////////////////////////

NetworkChannel::NetworkChannel() : m_socket(m_io_context) {}

NetworkChannel::~NetworkChannel()
{
  stop_messaging();
}

bool NetworkChannel::isConnected() const
{
  return m_socket.is_open();
}

void NetworkChannel::registerHandler(uint8_t type, MessageHandler handler)
{
  m_handlers[type] = handler;
}

void NetworkChannel::removeHandler(uint8_t messageType)
{
  m_handlers.erase(messageType);
}

void NetworkChannel::removeAllHandlers()
{
  m_handlers.clear();
}

void NetworkChannel::setConnectHandler(ConnectHandler handler)
{
  std::lock_guard lock(m_lifecycleMutex);
  m_connectHandler = std::move(handler);
}

void NetworkChannel::setDisconnectHandler(DisconnectHandler handler)
{
  std::lock_guard lock(m_lifecycleMutex);
  m_disconnectHandler = std::move(handler);
}

MessageFuture NetworkChannel::send(Message &&msg)
{
  using MessagePromise = std::promise<boost::system::error_code>;
  auto promise = std::make_shared<MessagePromise>();
  auto future = promise->get_future();

  if (!m_messagingActive.load() || !isConnected()) {
    log_asio_error(asio::error::not_connected, "Send");
    promise->set_value(asio::error::not_connected); // Set error in promise
    return future;
  }

  auto self = shared_from_this();
  auto pending = std::make_shared<PendingWrite>();
  pending->promise = promise;
  pending->completed = std::make_shared<std::atomic<bool>>(false);
  const auto payloadLength = static_cast<size_t>(msg.header.payload_length);
  assert(payloadLength == msg.payload.size());
  pending->wireData.resize(sizeof(Message::Header) + payloadLength);
  std::memcpy(pending->wireData.data(), &msg.header, sizeof(Message::Header));
  if (payloadLength > 0) {
    const auto bytesToCopy = std::min(payloadLength, msg.payload.size());
    std::memcpy(pending->wireData.data() + sizeof(Message::Header),
        msg.payload.data(),
        bytesToCopy);
  }

  boost::asio::post(m_io_context,
      [self, pending]() { self->enqueue_write(std::move(pending)); });

  return future;
}

MessageFuture NetworkChannel::send(uint8_t type, StructuredMessage &&msg)
{
  Message message = msg.toMessage(type);
  return send(std::move(message));
}

MessageFuture NetworkChannel::send(uint8_t type)
{
  return send(make_message(type));
}

MessageFuture NetworkChannel::send(uint8_t type, const std::string &str)
{
  return send(make_message(type, str));
}

void NetworkChannel::start_messaging()
{
  vsr::core::logDebug("[NetworkChannel] starting channel");
  stop_messaging();
  m_work.emplace(asio::make_work_guard(m_io_context));
  m_io_context.restart();
  m_messagingActive.store(true);
  m_io_thread = std::thread([this]() {
    vsr::core::logDebug("[NetworkChannel] starting IO thread");
    try {
      m_io_context.run();
    } catch (const std::exception &e) {
      vsr::core::logError(
          "[NetworkChannel] IO thread context error: %s", e.what());
    } catch (...) {
      vsr::core::logError("[NetworkChannel] IO thread context unknown error");
    }
    vsr::core::logDebug("[NetworkChannel] IO thread stopped");
  });
}

void NetworkChannel::stop_messaging()
{
  try {
    m_messagingActive.store(false);
    // The IO thread goes first: it may be closing this very socket after a
    // read error, and asio does not survive two threads closing one socket.
    // Once it is joined the close below is the only party touching it.
    m_io_context.stop();
    if (m_io_thread.joinable())
      m_io_thread.join();
    fail_pending_writes(asio::error::operation_aborted);
    close_socket(asio::error::operation_aborted);
    m_work.reset();

    // Run the completions the stop cut off (aborted reads and writes on the
    // closed socket) here, on the caller's thread. Left queued, they would
    // run on the next start_messaging()'s IO thread and their close_socket()
    // could tear down a freshly opened socket mid-connect. poll() only runs
    // them once the stopped context is restarted.
    m_io_context.restart();
    m_io_context.poll();
  } catch (const std::system_error &e) {
    vsr::core::logError(
        "[NetworkChannel] System error during stop: %s", e.what());
  } catch (const std::exception &e) {
    vsr::core::logError("[NetworkChannel] Error during stop: %s", e.what());
  }
}

void NetworkChannel::read_header()
{
  if (!isConnected()) {
    vsr::core::logError("[NetworkChannel] Cannot read header: not connected");
    return;
  }

  auto message = std::make_shared<Message>();
  auto self = shared_from_this();
  const auto generation = m_socketGeneration;
  asio::async_read(m_socket,
      asio::buffer(&message->header, sizeof(Message::Header)),
      [this, self, message, generation](const boost::system::error_code &error,
          std::size_t bytes_transferred) {
        if (generation != m_socketGeneration)
          return; // a cut-off read on a socket since replaced
        log_asio_error(error, "ReadHeader");
        if (!error)
          read_payload(message); // Read next message
      });
}

void NetworkChannel::read_payload(std::shared_ptr<Message> msg)
{
  if (!isConnected()) {
    vsr::core::logError("[NetworkChannel] Cannot read payload: not connected");
    return;
  }

  if (msg->header.payload_length == 0) {
    async_invoke(m_io_context, [this, msg]() {
      invoke_handler(msg);
      read_header(); // Read next msg
    });
    return;
  }

  msg->payload.resize(msg->header.payload_length);

  auto self = shared_from_this();
  const auto generation = m_socketGeneration;
  asio::async_read(m_socket,
      asio::buffer(msg->payload.data(), msg->header.payload_length),
      [this, self, msg, generation](const boost::system::error_code &error,
          std::size_t bytes_transferred) {
        if (generation != m_socketGeneration)
          return;
        log_asio_error(error, "ReadPayload");
        if (!error) {
          invoke_handler(msg);
          read_header(); // Read next msg
        }
      });
}

void NetworkChannel::invoke_handler(std::shared_ptr<Message> msg)
{
  // Invoke handler if registered
  if (auto *handler = m_handlers.at(msg->header.type); handler != nullptr) {
    (*handler)(*msg);
  } else {
    vsr::core::logWarning(
        "[NetworkChannel] No handler registered for message type %d",
        static_cast<int>(msg->header.type));
  }
}

void NetworkChannel::log_asio_error(
    const boost::system::error_code &error, const char *context)
{
  if (!error)
    return;

  if (error == asio::error::eof) {
    vsr::core::logStatus(
        "[NetworkChannel] %s: connection closed by peer", context);
  } else if (error == asio::error::connection_reset) {
    vsr::core::logStatus(
        "[NetworkChannel] %s: connection reset by peer", context);
  } else if (error == asio::error::not_connected) {
    vsr::core::logWarning("[NetworkChannel] %s: not connected", context);
  } else if (error) {
    vsr::core::logError(
        "[NetworkChannel] %s error: %s", context, error.message().c_str());
  }

  fail_pending_writes(error);
  close_socket(error);
}

void NetworkChannel::notify_connected()
{
  ++m_socketGeneration;
  m_disconnectReported.store(false);
  ConnectHandler handler;
  {
    std::lock_guard lock(m_lifecycleMutex);
    handler = m_connectHandler;
  }
  if (handler)
    handler();
}

void NetworkChannel::notify_disconnected(
    const boost::system::error_code &reason)
{
  if (m_disconnectReported.exchange(true))
    return;
  DisconnectHandler handler;
  {
    std::lock_guard lock(m_lifecycleMutex);
    handler = m_disconnectHandler;
  }
  if (handler)
    handler(reason);
}

void NetworkChannel::enqueue_write(std::shared_ptr<PendingWrite> pending)
{
  if (!m_messagingActive.load() || !isConnected()) {
    complete_write(pending, asio::error::not_connected);
    return;
  }

  {
    std::lock_guard lock(m_writeMutex);
    m_pendingWrites.push_back(std::move(pending));
    if (m_writeInProgress)
      return;
    m_writeInProgress = true;
  }

  start_next_write();
}

void NetworkChannel::start_next_write()
{
  std::shared_ptr<PendingWrite> pending;
  {
    std::lock_guard lock(m_writeMutex);
    if (m_pendingWrites.empty()) {
      m_writeInProgress = false;
      return;
    }
    pending = m_pendingWrites.front();
  }

  if (!m_messagingActive.load() || !isConnected()) {
    fail_pending_writes(asio::error::not_connected);
    return;
  }

  auto self = shared_from_this();
  const auto generation = m_socketGeneration;
  asio::async_write(m_socket,
      asio::buffer(pending->wireData),
      [self, pending, generation](
          const boost::system::error_code &error, std::size_t) {
        if (generation != self->m_socketGeneration) {
          // The socket was replaced under this write; its queue was failed
          // with it, so only the promise (if still open) needs settling.
          self->complete_write(
              pending, error ? error : asio::error::operation_aborted);
          return;
        }
        {
          std::lock_guard lock(self->m_writeMutex);
          if (!self->m_pendingWrites.empty()
              && self->m_pendingWrites.front() == pending) {
            self->m_pendingWrites.pop_front();
          }
        }

        self->complete_write(pending, error);
        if (error) {
          self->log_asio_error(error, "Send");
        } else {
          self->start_next_write();
        }
      });
}

void NetworkChannel::fail_pending_writes(const boost::system::error_code &error)
{
  std::deque<std::shared_ptr<PendingWrite>> pending;
  {
    std::lock_guard lock(m_writeMutex);
    pending.swap(m_pendingWrites);
    m_writeInProgress = false;
  }

  for (auto &p : pending)
    complete_write(p, error);
}

void NetworkChannel::complete_write(
    const std::shared_ptr<PendingWrite> &pending,
    const boost::system::error_code &error)
{
  if (!pending || !pending->completed || !pending->promise)
    return;

  if (pending->completed->exchange(true))
    return;

  pending->promise->set_value(error);
}

void NetworkChannel::close_socket(const boost::system::error_code &reason)
{
  if (m_socket.is_open()) {
    boost::system::error_code ec{};
    m_socket.shutdown(tcp::socket::shutdown_both, ec);
    m_socket.close(ec);
  }
  // The latch, not the socket state, decides whether this reports: a failed
  // connect can leave the socket closed yet still owes its one notification.
  notify_disconnected(reason);
}

// NetworkServer definitions //////////////////////////////////////////////////

NetworkServer::NetworkServer(short port)
    : m_acceptor(m_io_context, tcp::endpoint(tcp::v4(), port))
{
  start_accept();
}

unsigned short NetworkServer::port() const
{
  return m_acceptor.local_endpoint().port();
}

void NetworkServer::start()
{
  start_messaging();
}

void NetworkServer::restart()
{
  stop();
  start_accept();
  start();
}

void NetworkServer::stop()
{
  stop_messaging();
}

void NetworkServer::start_accept()
{
  if (m_acceptPending.exchange(true))
    return;

  auto socket = std::make_shared<tcp::socket>(m_io_context);
  m_acceptor.async_accept(
      *socket, [this, socket](const boost::system::error_code &error) {
        m_acceptPending.store(false);
        if (!error) {
          vsr::core::logStatus("[NetworkServer] New connection from %s",
              socket->remote_endpoint().address().to_string().c_str());
          if (m_socket.is_open()) {
            // A second client over a live one: the first connection ends
            // here, reported before the new one is announced, instead of
            // dying silently under the move below.
            vsr::core::logWarning(
                "[NetworkServer] New connection replaces the current one");
            fail_pending_writes(asio::error::connection_aborted);
            close_socket(asio::error::connection_aborted);
          }
          m_socket = std::move(*socket);
          notify_connected();
          read_header();
          start_accept(); // Accept next connection
        }
      });
}

// NetworkClient definitions //////////////////////////////////////////////////

NetworkClient::NetworkClient(const std::string &host, short port)
{
  connect(host, port);
}

void NetworkClient::connect(const std::string &host, short port)
{
  // start_messaging() tears down any previous connection first (reporting it
  // through the disconnect handler if it was still open), so a connect after
  // a failed or closed connection behaves like a first connect. The latch is
  // armed here so that a failure below is reported exactly once.
  start_messaging();
  const auto generation = ++m_connectGeneration;
  m_disconnectReported.store(false);

  // The port travels as `short` for API symmetry with NetworkServer, whose
  // tcp::endpoint converts it to unsigned; the resolver needs the same
  // unsigned spelling or ports above 32767 come out negative.
  const auto service = std::to_string(static_cast<unsigned short>(port));

  // Resolution can take as long as a DNS round trip, so it runs on the IO
  // thread like the connect itself; the caller never waits on the network.
  auto resolver = std::make_shared<tcp::resolver>(m_io_context);
  resolver->async_resolve(host,
      service,
      [this, resolver, generation, host, service](
          const boost::system::error_code &error,
          const tcp::resolver::results_type &endpoints) {
        if (generation != m_connectGeneration.load())
          return; // superseded by a later connect() or disconnect()
        if (error) {
          vsr::core::logError("[NetworkClient] Cannot resolve %s:%s: %s",
              host.c_str(),
              service.c_str(),
              error.message().c_str());
          close_socket(error);
          return;
        }

        asio::async_connect(m_socket,
            endpoints,
            [this, generation](
                const boost::system::error_code &error, const tcp::endpoint &) {
              if (generation != m_connectGeneration.load())
                return;
              if (!error) {
                vsr::core::logStatus("[NetworkClient] Connected to server");
                notify_connected();
                read_header();
              } else {
                vsr::core::logError("[NetworkClient] Connection error: %s",
                    error.message().c_str());
                close_socket(error);
              }
            });
      });
}

void NetworkClient::disconnect()
{
  ++m_connectGeneration;
  stop_messaging();
  vsr::core::logStatus("[NetworkClient] Disconnected from server");
}

} // namespace vsr::network
