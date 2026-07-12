#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DATA_DIR="${DATA_DIR:-$ROOT/data}"

for name in nm ss1 ss2; do
  pidfile="$DATA_DIR/logs/${name}.pid"
  if [[ -f "$pidfile" ]]; then
    pid="$(cat "$pidfile")"
    if kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
      echo "Stopped $name (pid $pid)"
    fi
    rm -f "$pidfile"
  fi
done

pkill -f "$ROOT/naming_server" 2>/dev/null || true
pkill -f "$ROOT/storage_server" 2>/dev/null || true
echo "Cluster stopped."
