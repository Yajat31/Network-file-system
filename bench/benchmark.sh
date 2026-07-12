#!/usr/bin/env bash
# Benchmark create / write / read / list against a local 2-storage-server cluster.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
DATA_DIR="$ROOT/bench/tmp"
OUT_MD="$ROOT/bench/results.md"
RAW="$ROOT/bench/results_raw.txt"
NM_PORT=28080
SS1_PORT=29001
SS2_PORT=29002

rm -rf "$DATA_DIR"
mkdir -p "$DATA_DIR/ss1" "$DATA_DIR/ss2" "$DATA_DIR/logs" "$DATA_DIR/payloads"

"$ROOT/scripts/stop_local.sh" >/dev/null 2>&1 || true
# stop any leftover bench ports
pkill -f "$ROOT/naming_server $NM_PORT" 2>/dev/null || true

make -s all
"$ROOT/naming_server" "$NM_PORT" >"$DATA_DIR/logs/nm.log" 2>&1 &
echo $! >"$DATA_DIR/logs/nm.pid"
sleep 0.4
"$ROOT/storage_server" 127.0.0.1 "$NM_PORT" "$SS1_PORT" "$DATA_DIR/ss1" 127.0.0.1 >"$DATA_DIR/logs/ss1.log" 2>&1 &
echo $! >"$DATA_DIR/logs/ss1.pid"
sleep 0.3
"$ROOT/storage_server" 127.0.0.1 "$NM_PORT" "$SS2_PORT" "$DATA_DIR/ss2" 127.0.0.1 >"$DATA_DIR/logs/ss2.log" 2>&1 &
echo $! >"$DATA_DIR/logs/ss2.pid"
sleep 0.5

CLIENT="$ROOT/client"
run_cmd() {
  printf '%s\nexit\n' "$1" | "$CLIENT" 127.0.0.1 "$NM_PORT" >/dev/null 2>&1
}

now_ms() {
  date +%s%3N
}

elapsed_ms() {
  local start="$1" end
  end="$(now_ms)"
  echo $((end - start))
}

: >"$RAW"

echo "=== Benchmarks ===" | tee -a "$RAW"

# --- create throughput ---
N_CREATE=50
start="$(now_ms)"
for i in $(seq 1 "$N_CREATE"); do
  run_cmd "create -f bench_$i.txt"
done
create_ms="$(elapsed_ms "$start")"
create_ops=$(awk -v n="$N_CREATE" -v ms="$create_ms" 'BEGIN{ if(ms==0) ms=1; printf "%.2f", n*1000/ms }')
echo "create: $N_CREATE files in ${create_ms}ms (${create_ops} ops/s)" | tee -a "$RAW"

# --- write / read by size ---
declare -a SIZES=(1024 65536 1048576 10485760)
declare -a LABELS=("1KB" "64KB" "1MB" "10MB")

printf '\n%-8s %12s %12s %12s %12s\n' "size" "write_ms" "write_MBps" "read_ms" "read_MBps" | tee -a "$RAW"
printf '%-8s %12s %12s %12s %12s\n' "----" "--------" "----------" "-------" "---------" | tee -a "$RAW"

for idx in "${!SIZES[@]}"; do
  size="${SIZES[$idx]}"
  label="${LABELS[$idx]}"
  payload="$DATA_DIR/payloads/p_${label}.bin"
  dd if=/dev/urandom of="$payload" bs="$size" count=1 status=none 2>/dev/null

  path="/bench_w_${label}.bin"
  run_cmd "create -f bench_w_${label}.bin"

  start="$(now_ms)"
  run_cmd "write --sync $path $payload"
  w_ms="$(elapsed_ms "$start")"
  w_mbps=$(awk -v s="$size" -v ms="$w_ms" 'BEGIN{ if(ms==0) ms=1; printf "%.2f", (s/1048576)/(ms/1000) }')

  start="$(now_ms)"
  run_cmd "read $path"
  r_ms="$(elapsed_ms "$start")"
  r_mbps=$(awk -v s="$size" -v ms="$r_ms" 'BEGIN{ if(ms==0) ms=1; printf "%.2f", (s/1048576)/(ms/1000) }')

  printf '%-8s %12s %12s %12s %12s\n' "$label" "$w_ms" "$w_mbps" "$r_ms" "$r_mbps" | tee -a "$RAW"
done

# --- list latency ---
N_LIST=30
start="$(now_ms)"
for i in $(seq 1 "$N_LIST"); do
  run_cmd "list /"
done
list_ms="$(elapsed_ms "$start")"
list_avg=$(awk -v n="$N_LIST" -v ms="$list_ms" 'BEGIN{ printf "%.2f", ms/n }')
echo | tee -a "$RAW"
echo "list: $N_LIST calls in ${list_ms}ms (avg ${list_avg}ms)" | tee -a "$RAW"

# --- concurrent clients (background processes) ---
N_CONC=8
start="$(now_ms)"
pids=()
for i in $(seq 1 "$N_CONC"); do
  (
    run_cmd "create -f conc_$i.txt"
    printf 'c%d\n' "$i" >"$DATA_DIR/payloads/c$i.txt"
    run_cmd "write --sync /conc_$i.txt $DATA_DIR/payloads/c$i.txt"
    run_cmd "read /conc_$i.txt"
  ) &
  pids+=($!)
done
for p in "${pids[@]}"; do wait "$p" || true; done
conc_ms="$(elapsed_ms "$start")"
echo "concurrent: $N_CONC clients (create+write+read) in ${conc_ms}ms" | tee -a "$RAW"

HOST="$(uname -n 2>/dev/null || echo unknown)"
KERNEL="$(uname -r 2>/dev/null || echo unknown)"
DATE="$(date -u +"%Y-%m-%d %H:%M:%S UTC")"

cat >"$OUT_MD" <<EOF
# Benchmark Results

Measured on \`$HOST\` (\`$KERNEL\`) at **$DATE** against a local 2-storage-server cluster
(naming server port $NM_PORT, storage ports $SS1_PORT / $SS2_PORT).

## Create throughput

| Metric | Value |
|--------|-------|
| Files created | $N_CREATE |
| Wall time | ${create_ms} ms |
| Throughput | **${create_ops} ops/s** |

## Write / read by payload size

| Size | Write (ms) | Write (MB/s) | Read (ms) | Read (MB/s) |
|------|------------|--------------|-----------|-------------|
EOF

idx=0
while IFS= read -r line; do
  # skip until we hit size table rows
  :
done <"$RAW"

# Parse size rows from RAW (lines that start with known labels)
for label in "${LABELS[@]}"; do
  row="$(grep -E "^${label}[[:space:]]" "$RAW" | head -1)"
  # shellcheck disable=SC2086
  set -- $row
  echo "| $1 | $2 | $3 | $4 | $5 |" >>"$OUT_MD"
done

cat >>"$OUT_MD" <<EOF

## List latency

| Metric | Value |
|--------|-------|
| Calls | $N_LIST |
| Total | ${list_ms} ms |
| Average | **${list_avg} ms** |

## Concurrent clients

| Metric | Value |
|--------|-------|
| Clients | $N_CONC |
| Workload | create + sync write + read each |
| Wall time | **${conc_ms} ms** |

## Notes

- Times include client CLI startup/teardown per command (\`printf | client\`), so absolute
  numbers are conservative compared to a long-lived client session.
- Storage servers advertise \`127.0.0.1\`; payloads are generated with \`dd\` from \`/dev/urandom\`.
- Re-run with: \`./bench/benchmark.sh\`
EOF

echo
echo "Wrote $OUT_MD"
cat "$OUT_MD"

# cleanup
kill "$(cat "$DATA_DIR/logs/nm.pid")" 2>/dev/null || true
kill "$(cat "$DATA_DIR/logs/ss1.pid")" 2>/dev/null || true
kill "$(cat "$DATA_DIR/logs/ss2.pid")" 2>/dev/null || true
pkill -f "$ROOT/naming_server $NM_PORT" 2>/dev/null || true
pkill -f "$ROOT/storage_server" 2>/dev/null || true
