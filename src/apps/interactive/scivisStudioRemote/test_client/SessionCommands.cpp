// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

/*
 * The CommandRunner handlers for the connection and the frame stream:
 * connect, disconnect, shutdown, ping, expect-pong, await-lost, reconnect,
 * sleep, expect-error, send-raw, set-frame-config, set-encodings,
 * start-rendering, stop-rendering, await-frame, await-frame-at,
 * await-frame-advance, await-warning and save-frame. The table in
 * CommandRunner.cpp has checked each command's argument count before a
 * handler runs; what is left to check is the arguments' values.
 */

#include "CommandRunner.h"
#include "CommandText.h"
// vsr_scivis_studio_protocol
#include "FrameCodec.h"
// std
#include <cctype>
#include <cstdint>
#include <fstream>
#include <sstream>

namespace vsr::scivis_studio::test_client {

using namespace protocol;

namespace {

bool parseHexBytes(const std::vector<std::string> &tokens,
    size_t first,
    std::vector<std::byte> &out,
    std::string &error)
{
  out.clear();
  for (size_t i = first; i < tokens.size(); ++i) {
    const auto &token = tokens[i];
    if (token.empty() || token.size() % 2 != 0) {
      error = "hex bytes need an even number of digits: " + token;
      return false;
    }
    for (size_t j = 0; j < token.size(); j += 2) {
      // Two hex digits exactly: strtol's tolerance of "-1" or "+f" is not
      // wanted on a byte.
      const std::string pair = token.substr(j, 2);
      if (!std::isxdigit(static_cast<unsigned char>(pair[0]))
          || !std::isxdigit(static_cast<unsigned char>(pair[1]))) {
        error = "not a hex byte: " + pair;
        return false;
      }
      out.push_back(std::byte(std::stoul(pair, nullptr, 16)));
    }
  }
  return true;
}

bool writePPM(const std::string &path,
    uint32_t width,
    uint32_t height,
    const std::vector<uint8_t> &rgba,
    std::string &error)
{
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    error = "cannot open " + path + " for writing";
    return false;
  }
  file << "P6\n" << width << ' ' << height << "\n255\n";
  std::vector<char> rgb;
  rgb.reserve(size_t(width) * height * 3);
  for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
    rgb.push_back(char(rgba[i]));
    rgb.push_back(char(rgba[i + 1]));
    rgb.push_back(char(rgba[i + 2]));
  }
  file.write(rgb.data(), std::streamsize(rgb.size()));
  if (!file) {
    error = "write to " + path + " failed";
    return false;
  }
  return true;
}

} // namespace

// Session ////////////////////////////////////////////////////////////////////

CommandRunner::Failure CommandRunner::connect(
    const Command &command, Deadline deadline)
{
  if (m_session->state() == SessionState::Connected)
    return "already connected to " + m_session->host() + ":"
        + std::to_string(m_session->port());
  std::string host = m_options.host;
  uint16_t port = m_options.port;
  if (!command.args.empty())
    host = command.args[0];
  if (command.args.size() > 1 && !parsePort(command.args[1], port))
    return "port must be an integer in 1..65535, got: " + command.args[1];

  std::string error;
  const bool ok = m_session->connect(host, port, deadline, &error);
  const auto pending = drainEvents();
  if (!ok)
    return error;
  // The Bootstrap's snapshot is not the one await-snapshot waits for.
  m_snapshotMark = m_session->snapshotsReceived();
  m_pendingReplies.clear();
  return pending;
}

CommandRunner::Failure CommandRunner::disconnect(const Command &)
{
  if (m_session->state() != SessionState::Connected
      && m_session->state() != SessionState::Lost)
    return notConnected();
  // Before the close: whatever the server said last still counts.
  const auto pending = drainEvents();
  m_session->disconnect();
  return pending;
}

CommandRunner::Failure CommandRunner::shutdown(
    const Command &, Deadline deadline)
{
  std::string error;
  // Events keep flowing while the socket is still open; print them after.
  const bool ok = m_session->shutdown(deadline, &error);
  const auto pending = drainEvents();
  if (!ok)
    return error;
  return pending;
}

CommandRunner::Failure CommandRunner::ping(const Command &)
{
  std::string error;
  if (!m_session->ping(&error))
    return error;
  // No drain: the Pong belongs to the expect-pong that usually follows.
  return {};
}

CommandRunner::Failure CommandRunner::expectPong(
    const Command &, Deadline deadline)
{
  // Frames are stream data, not replies; any other message is the answer.
  Event next;
  const auto wait = pumpUntilEvent(
      [](const Event &e) { return e.name != "Frame"; }, deadline, &next);
  if (wait != WaitEnd::Done)
    return waitFailure(wait, "Pong", deadline);
  if (next.name != "Pong")
    return "expected Pong, got " + next.text();
  return {};
}

CommandRunner::Failure CommandRunner::awaitLost(
    const Command &, Deadline deadline)
{
  const auto wait = pumpUntil(
      [&] { return m_session->state() == SessionState::Lost; }, deadline);
  if (wait != WaitEnd::Done) {
    return wait == WaitEnd::Error
        ? waitFailure(wait, "the loss", deadline)
        : std::string("still ") + toString(m_session->state()) + " after "
            + std::to_string(deadline.count()) + " ms";
  }
  return {};
}

CommandRunner::Failure CommandRunner::reconnect(
    const Command &, Deadline deadline)
{
  if (m_session->state() == SessionState::Connected)
    return "already connected";
  std::string error;
  const bool ok = m_session->reconnect(deadline, &error);
  const auto pending = drainEvents();
  if (!ok)
    return error;
  m_snapshotMark = m_session->snapshotsReceived();
  m_pendingReplies.clear();
  return pending;
}

CommandRunner::Failure CommandRunner::sleep(const Command &command)
{
  std::chrono::milliseconds ms;
  if (!parseMilliseconds(command.args[0], ms))
    return usageError(command);
  // Passing time is the point, so a loss meanwhile is the next command's to
  // notice; an Error is still an answer nobody asked for.
  const auto wait = pumpUntil([] { return false; }, ms, LossEnds::Nothing);
  if (wait == WaitEnd::Error)
    return waitFailure(wait, "the sleep to end", ms);
  return {};
}

CommandRunner::Failure CommandRunner::expectError(
    const Command &command, Deadline deadline)
{
  // Frames are stream data, not replies: one still in flight after
  // stop-rendering must not stand in for the answer. Nor is a Pong: the
  // session pings on its own after a quiet spell.
  Event next;
  const auto wait = pumpUntilEvent(
      [](const Event &e) { return e.name != "Frame" && e.name != "Pong"; },
      deadline,
      &next);
  if (wait != WaitEnd::Done)
    return waitFailure(wait, "server message", deadline);
  if (next.name != "Error")
    return "expected Error, got " + next.text();
  if (!command.args.empty()
      && m_session->lastError().find(command.args[0]) == std::string::npos) {
    return "Error " + quoted(m_session->lastError()) + " does not contain "
        + quoted(command.args[0]);
  }
  return {};
}

CommandRunner::Failure CommandRunner::sendRaw(const Command &command)
{
  unsigned long long type = 0;
  if (!parseNonNegative(command.args[0], type) || type > 0xff)
    return usageError(command);
  std::vector<std::byte> payload;
  std::string error;
  if (!parseHexBytes(command.args, 1, payload, error))
    return error;
  if (!m_session->sendRaw(uint8_t(type), std::move(payload), &error))
    return error;
  // No drain: the reply belongs to the expect-error that usually follows.
  return {};
}

// The frame stream ///////////////////////////////////////////////////////////

CommandRunner::Failure CommandRunner::setFrameConfig(
    const Command &command, Deadline deadline)
{
  unsigned long long width = 0;
  unsigned long long height = 0;
  if (!parseNonNegative(command.args[0], width)
      || !parseNonNegative(command.args[1], height))
    return usageError(command);
  std::string error;
  if (!m_session->setFrameConfig(uint32_t(width), uint32_t(height), &error))
    return error;
  const auto wait = pumpUntilEvent(
      [](const Event &e) { return e.name == "FrameConfig"; }, deadline);
  if (wait != WaitEnd::Done)
    return waitFailure(wait, "FrameConfig ack", deadline);
  return {};
}

CommandRunner::Failure CommandRunner::setEncodings(const Command &command)
{
  std::vector<FrameEncoding> preferred;
  std::stringstream list(command.args[0]);
  std::string item;
  while (std::getline(list, item, ',')) {
    const auto wanted = lower(item);
    std::optional<FrameEncoding> match;
    for (auto encoding : {FrameEncoding::Raw, FrameEncoding::TurboJpeg}) {
      if (lower(toString(encoding)) == wanted)
        match = encoding;
    }
    if (!match)
      return "unknown frame encoding '" + item + "'; valid: raw, turbojpeg";
    preferred.push_back(*match);
  }
  std::string error;
  if (!m_session->setEncodings(preferred, &error))
    return error;
  return drainEvents();
}

CommandRunner::Failure CommandRunner::startRendering(const Command &)
{
  std::string error;
  if (!m_session->startRendering(&error))
    return error;
  return drainEvents();
}

CommandRunner::Failure CommandRunner::stopRendering(const Command &)
{
  std::string error;
  if (!m_session->stopRendering(&error))
    return error;
  return drainEvents();
}

CommandRunner::Failure CommandRunner::awaitFrame(
    const Command &command, Deadline deadline)
{
  unsigned long long count = 1;
  if (!command.args.empty() && !parseNonNegative(command.args[0], count))
    return usageError(command);
  if (const auto lost = notConnected())
    return lost;
  size_t seen = 0;
  const auto wait = pumpUntilEvent(
      [&](const Event &e) {
        if (e.name == "Frame")
          ++seen;
        return seen >= count;
      },
      deadline);
  if (wait != WaitEnd::Done) {
    return waitFailure(wait,
        "frame " + std::to_string(seen + 1) + " of " + std::to_string(count),
        deadline);
  }
  return {};
}

CommandRunner::Failure CommandRunner::awaitFrameAt(
    const Command &command, Deadline deadline)
{
  long long frame = 0;
  if (!parseInteger(command.args[0], frame))
    return usageError(command);
  if (const auto lost = notConnected())
    return lost;
  // Header fields are text in the record; the frame number compares as such.
  const auto wanted = std::to_string(frame);
  const auto wait = pumpUntilEvent(
      [&](const Event &e) {
        if (e.name != "Frame")
          return false;
        for (const auto &[key, value] : e.fields)
          if (key == "frame")
            return value == wanted;
        return false;
      },
      deadline);
  if (wait != WaitEnd::Done)
    return waitFailure(wait, "a Frame at frame " + wanted, deadline);
  return {};
}

CommandRunner::Failure CommandRunner::awaitFrameAdvance(
    const Command &command, Deadline deadline)
{
  unsigned long long count = 1;
  if (!command.args.empty() && !parseNonNegative(command.args[0], count))
    return usageError(command);
  if (const auto lost = notConnected())
    return lost;
  // Counted by the session as frames are consumed: a header whose frame
  // differs from the previous one's is one advance.
  const size_t target = m_session->framesAdvanced() + count;
  const auto wait = pumpUntil(
      [&] { return m_session->framesAdvanced() >= target; }, deadline);
  if (wait != WaitEnd::Done) {
    const auto seen = target - m_session->framesAdvanced();
    return waitFailure(wait,
        "frame advance " + std::to_string(count - seen + 1) + " of "
            + std::to_string(count),
        deadline);
  }
  return {};
}

CommandRunner::Failure CommandRunner::awaitWarning(
    const Command &, Deadline deadline)
{
  if (const auto lost = notConnected())
    return lost;
  const auto wait = pumpUntilEvent(
      [](const Event &e) { return e.name == "TimeAdvanceWarning"; }, deadline);
  if (wait != WaitEnd::Done)
    return waitFailure(wait, "TimeAdvanceWarning", deadline);
  return {};
}

CommandRunner::Failure CommandRunner::saveFrame(const Command &command)
{
  if (!m_session->lastFrameHeader())
    return "no frame received yet";
  const auto view = decodeFrame(m_session->lastFrame());
  if (!view)
    return "the last frame does not decode";
  std::vector<uint8_t> pixels;
  if (!decodeFramePixels(*view, pixels)) {
    return std::string("cannot decode ") + toString(view->header.encoding)
        + " pixels in this build";
  }
  std::string error;
  if (!writePPM(command.args[0],
          view->header.width,
          view->header.height,
          pixels,
          error))
    return error;
  return {};
}

} // namespace vsr::scivis_studio::test_client
