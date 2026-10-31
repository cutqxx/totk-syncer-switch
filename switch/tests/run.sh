#!/usr/bin/env bash
# Starts the mock WebDAV server on a free port and runs the host tests.
set -euo pipefail
cd "$(dirname "$0")"
out="$(cd "${1:-build}" && pwd)"
port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
python3 mock_webdav.py "$port" "$out/requests.json" > "$out/mock.log" 2>&1 &
pid=$!
trap 'kill $pid 2>/dev/null || true' EXIT
for _ in $(seq 50); do grep -q ready "$out/mock.log" 2>/dev/null && break; sleep 0.1; done
# Don't let the agent/system proxy intercept 127.0.0.1.
NO_PROXY=127.0.0.1,localhost no_proxy=127.0.0.1,localhost "$out/test_host" "$port" "$out"
