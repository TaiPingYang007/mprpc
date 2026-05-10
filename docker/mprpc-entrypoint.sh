#!/usr/bin/env bash
set -euo pipefail

wait_for_port() {
  local host="$1"
  local port="$2"
  local timeout="${3:-60}"
  local deadline=$((SECONDS + timeout))

  while (( SECONDS < deadline )); do
    if nc -z "$host" "$port" >/dev/null 2>&1; then
      return 0
    fi
    sleep 1
  done

  echo "timeout waiting for ${host}:${port}" >&2
  return 1
}

if [[ "${1:-}" == "smoke-test" ]]; then
  wait_for_port "zookeeper" "2181" 60
  wait_for_port "userservice-1" "8000" 60
  wait_for_port "userservice-2" "8000" 60
  wait_for_port "friendservice-1" "8000" 60

  /workspace/bin/calluserservice
  /workspace/bin/callfriendservice
  exit 0
fi

exec "$@"
