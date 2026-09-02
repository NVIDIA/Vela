// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Script.h"
#include "TestSession.h"
// vsr_scivis_studio_protocol
#include "StudioEndpoint.h"
// std
#include <chrono>
#include <cstddef>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace vsr::scivis_studio::test_client {

struct RunnerOptions
{
  // Where `connect` goes when the command names no host or port.
  std::string host{"127.0.0.1"};
  int port{protocol::DEFAULT_PORT};
  // Deadline of every waiting command unless it carries `timeout=<ms>`.
  std::chrono::milliseconds timeout{5000};
  // Carry on after a FAIL instead of stopping the script.
  bool keepGoing{false};
  // Drop the `EVT` lines for server messages; the `dump-*` commands still
  // print theirs.
  bool quietEvents{false};
};

/*
 * Executes script commands against a TestSession and writes the record
 * stream: exactly one `OK <command>` or `FAIL <command>: <reason>` line per
 * command, and one `EVT <Name> key=value ...` line for every server message
 * as the session consumes it. Every line is one record, so a shell script or
 * a reader can grep the output.
 *
 * The command vocabulary is the milestone-3 server surface: session
 * (connect, disconnect, shutdown, ping, await-lost, reconnect, sleep,
 * expect-error, send-raw), rendering (set-frame-config, set-encodings,
 * start-rendering, stop-rendering, await-frame, save-frame), scene edits
 * (set-param, remove-param, set-node-transform), inspection (dump-scene,
 * dump-layers, dump-project, dump-frame) and `assert <value> <op> <rhs>` over
 * the named values assertNames() lists. Waiting commands take a trailing
 * `timeout=<ms>`.
 *
 * Example:
 *   TestSession session;
 *   CommandRunner runner(&session, &std::cout, options);
 *   std::vector<Command> commands;
 *   parseScript("connect; start-rendering; await-frame 3", commands);
 *   return runner.run(commands) ? 0 : 1;
 */
struct CommandRunner
{
  CommandRunner(
      TestSession *session, std::ostream *out, RunnerOptions options = {});

  // Runs the commands in order, stopping at the first FAIL unless keepGoing.
  // True iff every command printed OK.
  bool run(const std::vector<Command> &commands);
  // One command: its events, then its OK or FAIL record. True on OK.
  bool runCommand(const Command &command);
  size_t failures() const;

  // Every value `assert` can name, as documented in --help; the pattern
  // param.<type>.<index>.<name> is listed as written.
  static const std::vector<std::string> &assertNames();
  // One line per command for --help.
  static const std::vector<std::string> &commandHelp();

 private:
  // Why a command FAILed; empty when it is OK.
  using Failure = std::optional<std::string>;
  using Deadline = std::chrono::milliseconds;

  Failure execute(Command command);

  Failure connect(const Command &, Deadline);
  Failure disconnect(const Command &);
  Failure shutdown(const Command &, Deadline);
  Failure ping(const Command &, Deadline);
  Failure awaitLost(const Command &, Deadline);
  Failure reconnect(const Command &, Deadline);
  Failure sleep(const Command &);
  Failure expectError(const Command &, Deadline);
  Failure sendRaw(const Command &);
  Failure setFrameConfig(const Command &, Deadline);
  Failure setEncodings(const Command &);
  Failure startRendering(const Command &);
  Failure stopRendering(const Command &);
  Failure awaitFrame(const Command &, Deadline);
  Failure saveFrame(const Command &);
  Failure setParam(const Command &);
  Failure removeParam(const Command &);
  Failure setNodeTransform(const Command &);
  Failure dumpScene(const Command &);
  Failure dumpLayers(const Command &);
  Failure dumpProject(const Command &);
  Failure dumpFrame(const Command &);
  Failure assertValue(const Command &);

  // The current text of a named value; empty with the reason when it is
  // unknown or not available yet (no frame, no replica, ...).
  std::optional<std::string> namedValue(
      const std::string &name, std::string &error);

  // Polls, printing events as they come, until `done` holds or the deadline
  // passes; false on timeout.
  bool pumpUntil(const std::function<bool()> &done, Deadline deadline);
  // Polls until an event `accept`s (that event is returned in `matched`).
  bool pumpUntilEvent(const std::function<bool(const Event &)> &accept,
      Deadline deadline,
      Event *matched = nullptr);
  // Prints whatever events are already queued without waiting.
  void drainEvents();
  void printEvent(const Event &event);
  void printRecord(const std::string &line);

  TestSession *m_session{nullptr};
  std::ostream *m_out{nullptr};
  RunnerOptions m_options;
  size_t m_failures{0};
};

} // namespace vsr::scivis_studio::test_client
