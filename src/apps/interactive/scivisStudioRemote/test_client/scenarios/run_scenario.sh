#!/usr/bin/env bash
## SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0
#
# Runs one scenario script against a freshly started scivisStudioServer.
#
#   run_scenario.sh <server-binary> <client-binary> <scenario.studio> [server args...]
#
# Picks a free TCP port, starts the server with `--library helide --data-root
# <tmp>` (plus any extra server args) in a temporary directory, waits for it
# to listen, runs the client with `--script <scenario>` from that directory
# (so relative save-frame paths land there), and exits with the client's exit
# code. The server is always stopped; on failure both logs are printed.
#
# Exit 77 (ctest's SKIP_RETURN_CODE for these tests) when the server cannot
# load any ANARI device, so a tree without helide skips instead of failing.
#
# A scenario whose first line is `# runner: kill-restart-after <n>` is run in
# kill-restart mode: after the client has printed <n> `OK` records the server
# is killed (SIGKILL, the "process went away" case), and a new one is started
# on the same port. loss.studio uses this to script server loss and recovery.

set -u

if [ $# -lt 3 ]; then
  echo "usage: $0 <server-binary> <client-binary> <scenario.studio> [server args...]" >&2
  exit 2
fi

server_bin=$1
client_bin=$2
scenario=$3
shift 3
server_args=("$@")

if [ ! -x "$server_bin" ]; then echo "not executable: $server_bin" >&2; exit 2; fi
if [ ! -x "$client_bin" ]; then echo "not executable: $client_bin" >&2; exit 2; fi
if [ ! -r "$scenario" ]; then echo "not readable: $scenario" >&2; exit 2; fi

name=$(basename "$scenario" .studio)
work=$(mktemp -d "${TMPDIR:-/tmp}/vsrStudioScenario-$name-XXXXXX")
data_root=$work/data
mkdir -p "$data_root"
server_log=$work/server.log
client_log=$work/client.log
client_err=$work/client-stderr.log
server_pid=""
client_pid=""

# The runner hint on the first line, if any.
kill_after=""
first_line=$(head -n 1 "$scenario")
if [[ $first_line =~ ^#[[:space:]]*runner:[[:space:]]*kill-restart-after[[:space:]]+([0-9]+) ]]; then
  kill_after=${BASH_REMATCH[1]}
fi

log() { echo "[run_scenario] $*" >&2; }

stop_server() {
  if [ -n "$server_pid" ] && kill -0 "$server_pid" 2>/dev/null; then
    kill -TERM "$server_pid" 2>/dev/null
    for _ in $(seq 1 50); do
      kill -0 "$server_pid" 2>/dev/null || break
      sleep 0.1
    done
    kill -KILL "$server_pid" 2>/dev/null
  fi
  [ -n "$server_pid" ] && wait "$server_pid" 2>/dev/null
  server_pid=""
}

cleanup() {
  if [ -n "$client_pid" ] && kill -0 "$client_pid" 2>/dev/null; then
    kill -TERM "$client_pid" 2>/dev/null
    wait "$client_pid" 2>/dev/null
  fi
  stop_server
}
trap cleanup EXIT INT TERM

dump_logs() {
  echo "==== server log ($server_log)" >&2
  cat "$server_log" >&2 2>/dev/null
  echo "==== client stderr ($client_err)" >&2
  cat "$client_err" >&2 2>/dev/null
  echo "====" >&2
}

# A port nobody listens on right now: ask the OS through python3 when it is
# there, otherwise probe random ports with bash's /dev/tcp.
pick_port() {
  if command -v python3 >/dev/null 2>&1; then
    python3 - <<'PY' && return 0
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
  fi
  for _ in $(seq 1 50); do
    local candidate=$((20000 + RANDOM % 40000))
    if ! (exec 3<>"/dev/tcp/127.0.0.1/$candidate") 2>/dev/null; then
      echo "$candidate"
      return 0
    fi
  done
  return 1
}

# Starts the server on $port; waits for it to reach Listening (its own status
# line, so the wait does not open a connection the server would see as a
# client). Returns 77 when no ANARI device loads, 1 on any other startup
# failure.
start_server() {
  : > "$server_log"
  "$server_bin" --port "$port" --library helide --data-root "$data_root" \
    ${server_args[@]+"${server_args[@]}"} >"$server_log" 2>&1 &
  server_pid=$!
  local deadline=$((SECONDS + 30))
  while [ $SECONDS -lt $deadline ]; do
    if grep -q "Listening on port $port" "$server_log" 2>/dev/null; then
      return 0
    fi
    if ! kill -0 "$server_pid" 2>/dev/null; then
      wait "$server_pid" 2>/dev/null
      server_pid=""
      if grep -q "no ANARI device could be loaded" "$server_log"; then
        log "server loaded no ANARI device; skipping"
        cat "$server_log" >&2
        return 77
      fi
      log "server exited before listening"
      return 1
    fi
    sleep 0.1
  done
  log "server did not start listening within 30 s"
  return 1
}

count_ok() {
  grep -c '^OK ' "$client_log" 2>/dev/null || true
}

port=$(pick_port) || { log "no free port found"; exit 1; }
log "scenario $name on port $port (work dir $work)"

start_server
status=$?
if [ $status -ne 0 ]; then
  [ $status -eq 77 ] || dump_logs
  exit $status
fi

(cd "$work" && exec "$client_bin" --port "$port" --script "$scenario") \
  >"$client_log" 2>"$client_err" &
client_pid=$!

if [ -n "$kill_after" ]; then
  # Wait until the client has passed the <n>-th command, then take the server
  # away and bring a new one up on the same port.
  deadline=$((SECONDS + 60))
  while [ "$(count_ok)" -lt "$kill_after" ]; do
    if ! kill -0 "$client_pid" 2>/dev/null; then break; fi
    if [ $SECONDS -ge $deadline ]; then
      log "client never reached OK record $kill_after"
      break
    fi
    sleep 0.1
  done
  if kill -0 "$client_pid" 2>/dev/null; then
    log "killing the server after $(count_ok) OK records"
    kill -KILL "$server_pid" 2>/dev/null
    wait "$server_pid" 2>/dev/null
    server_pid=""
    cp "$server_log" "$work/server-1.log"
    log "restarting the server"
    start_server
    status=$?
    if [ $status -ne 0 ]; then
      log "server restart failed"
      kill -TERM "$client_pid" 2>/dev/null
      wait "$client_pid" 2>/dev/null
      client_pid=""
      dump_logs
      exit 1
    fi
  fi
fi

wait "$client_pid"
client_status=$?
client_pid=""

# The client's record stream is the test's output either way; the transport
# and session logs on its stderr only matter when something went wrong.
cat "$client_log"

if [ $client_status -ne 0 ]; then
  log "client exited with $client_status"
  dump_logs
else
  log "client exited with 0"
fi
stop_server
exit $client_status
