# Benchmark Results

Measured on `cachyos` (`7.1.3-2-cachyos`) at **2026-07-12 18:51:47 UTC** against a local 2-storage-server cluster
(naming server port 28080, storage ports 29001 / 29002).

## Create throughput

| Metric | Value |
|--------|-------|
| Files created | 50 |
| Wall time | 147 ms |
| Throughput | **340.14 ops/s** |

## Write / read by payload size

| Size | Write (ms) | Write (MB/s) | Read (ms) | Read (MB/s) |
|------|------------|--------------|-----------|-------------|
| 1KB | 4 | 0.24 | 4 | 0.24 |
| 64KB | 4 | 15.62 | 4 | 15.62 |
| 1MB | 4 | 250.00 | 6 | 166.67 |
| 10MB | 22 | 454.55 | 26 | 384.62 |

## List latency

| Metric | Value |
|--------|-------|
| Calls | 30 |
| Total | 78 ms |
| Average | **2.60 ms** |

## Concurrent clients

| Metric | Value |
|--------|-------|
| Clients | 8 |
| Workload | create + sync write + read each |
| Wall time | **14 ms** |

## Notes

- Times include client CLI startup/teardown per command (`printf | client`), so absolute
  numbers are conservative compared to a long-lived client session.
- Storage servers advertise `127.0.0.1`; payloads are generated with `dd` from `/dev/urandom`.
- Re-run with: `./bench/benchmark.sh`
