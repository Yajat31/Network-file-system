#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

NM_PORT="${NM_PORT:-8080}"
SS1_PORT="${SS1_PORT:-9001}"
SS2_PORT="${SS2_PORT:-9002}"
DATA_DIR="${DATA_DIR:-$ROOT/data}"

mkdir -p "$DATA_DIR/ss1" "$DATA_DIR/ss2" "$DATA_DIR/logs"

make -s all

pkill -f "$ROOT/naming_server" 2>/dev/null || true
pkill -f "$ROOT/storage_server" 2>/dev/null || true
sleep 0.3

echo "Starting naming server on port $NM_PORT..."
"$ROOT/naming_server" "$NM_PORT" >"$DATA_DIR/logs/nm.log" 2>&1 &
echo $! >"$DATA_DIR/logs/nm.pid"
sleep 0.4

echo "Starting storage server 1 on port $SS1_PORT..."
"$ROOT/storage_server" 127.0.0.1 "$NM_PORT" "$SS1_PORT" "$DATA_DIR/ss1" 127.0.0.1 \
  >"$DATA_DIR/logs/ss1.log" 2>&1 &
echo $! >"$DATA_DIR/logs/ss1.pid"
sleep 0.4

echo "Starting storage server 2 on port $SS2_PORT..."
"$ROOT/storage_server" 127.0.0.1 "$NM_PORT" "$SS2_PORT" "$DATA_DIR/ss2" 127.0.0.1 \
  >"$DATA_DIR/logs/ss2.log" 2>&1 &
echo $! >"$DATA_DIR/logs/ss2.pid"
sleep 0.5

echo "Cluster ready."
echo "  Naming server : 127.0.0.1:$NM_PORT"
echo "  Storage #1    : 127.0.0.1:$SS1_PORT  root=$DATA_DIR/ss1"
echo "  Storage #2    : 127.0.0.1:$SS2_PORT  root=$DATA_DIR/ss2"
echo "Client: $ROOT/client 127.0.0.1 $NM_PORT"
echo "Stop with: scripts/stop_local.sh"
