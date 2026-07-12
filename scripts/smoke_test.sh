#!/usr/bin/env bash
# Non-interactive smoke test of core NFS operations.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
DATA_DIR="$ROOT/data"
NM_PORT=18080
SS1_PORT=19001
SS2_PORT=19002
export NM_PORT SS1_PORT SS2_PORT DATA_DIR

"$ROOT/scripts/stop_local.sh" >/dev/null 2>&1 || true
rm -rf "$DATA_DIR"
mkdir -p "$DATA_DIR/ss1" "$DATA_DIR/ss2" "$DATA_DIR/logs" "$DATA_DIR/local"

# Seed a local file for write tests
printf 'hello from local write\n' >"$DATA_DIR/local/sample.txt"
dd if=/dev/urandom of="$DATA_DIR/local/mid.bin" bs=1024 count=64 status=none

# Start cluster with custom ports
make -s all
"$ROOT/naming_server" "$NM_PORT" >"$DATA_DIR/logs/nm.log" 2>&1 &
echo $! >"$DATA_DIR/logs/nm.pid"
sleep 0.5
"$ROOT/storage_server" 127.0.0.1 "$NM_PORT" "$SS1_PORT" "$DATA_DIR/ss1" 127.0.0.1 >"$DATA_DIR/logs/ss1.log" 2>&1 &
echo $! >"$DATA_DIR/logs/ss1.pid"
sleep 0.4
"$ROOT/storage_server" 127.0.0.1 "$NM_PORT" "$SS2_PORT" "$DATA_DIR/ss2" 127.0.0.1 >"$DATA_DIR/logs/ss2.log" 2>&1 &
echo $! >"$DATA_DIR/logs/ss2.pid"
sleep 0.6

run_cmd() {
  # Feed a single command then exit to the interactive client
  printf '%s\nexit\n' "$1" | "$ROOT/client" 127.0.0.1 "$NM_PORT" 2>&1 | sed '/^Network File System/d;/^Type /d;/^> /d;/^$/d'
}

pass=0
fail=0
check() {
  local name="$1"
  local out="$2"
  local expect="$3"
  if echo "$out" | grep -qE "$expect"; then
    echo "PASS  $name"
    pass=$((pass+1))
  else
    echo "FAIL  $name"
    echo "  output: $out"
    fail=$((fail+1))
  fi
}

echo "=== Smoke tests ==="

out="$(run_cmd 'create -f hello.txt')"
check "create file" "$out" "Created"

out="$(run_cmd 'create -d docs')"
check "create dir" "$out" "Created"

out="$(run_cmd 'list /')"
check "list root" "$out" "hello.txt|/docs"

out="$(run_cmd "write --sync /hello.txt $DATA_DIR/local/sample.txt")"
check "write sync" "$out" "Write complete"

out="$(run_cmd 'read /hello.txt')"
check "read file" "$out" "hello from local write"

out="$(run_cmd 'info /hello.txt')"
check "info file" "$out" "[0-9]+"

out="$(run_cmd 'create -f /docs notes.txt')"
check "create nested file" "$out" "Created"

out="$(run_cmd "write --sync /docs/notes.txt $DATA_DIR/local/sample.txt")"
check "write nested" "$out" "Write complete"

out="$(run_cmd 'copy /hello.txt /docs')"
check "copy file" "$out" "Copied"

out="$(run_cmd 'list /docs')"
check "list docs" "$out" "notes.txt|hello.txt"

out="$(run_cmd 'delete /docs/notes.txt')"
check "delete file" "$out" "Deleted"

out="$(run_cmd 'list /docs')"
check "list after delete" "$out" "hello.txt"

echo
echo "Results: $pass passed, $fail failed"
"$ROOT/scripts/stop_local.sh" >/dev/null 2>&1 || true

if [[ "$fail" -ne 0 ]]; then
  echo "---- nm.log ----"
  tail -40 "$DATA_DIR/logs/nm.log" || true
  echo "---- ss1.log ----"
  tail -40 "$DATA_DIR/logs/ss1.log" || true
  exit 1
fi
exit 0
