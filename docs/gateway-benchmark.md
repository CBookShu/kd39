# Gateway Test And Benchmark

This document describes the regression tests and benchmark entry points for `access_gateway` HTTP/WS runtime validation.

Related docs:

- `README.md`
- `agent-quickstart.md`
- `services/access-gateway-architecture.md`
- `services/access-gateway-single-port-design.md`
- `gateway-async-roadmap.md`

## Integration Tests

- File: `tests/integration/gateway_async_test.cpp`
- Scope:
  - Real HTTP listener with concurrent requests.
  - Real WS listener with structured error response and normal route round-trip.
  - WS oversized message guard (`read_message_max`) and oversized downstream response guard (`payload_too_large`).
  - Router retry budget impact on timeout tail latency.
- Build note:
  - Requires GTest in the current build profile.
  - Existing CI/build profile may skip tests when GTest is unavailable.

## Benchmark Binary

- Target: `gateway_http_ws_bench`
- Source: `bench/gateway_http_ws_bench.cpp`
- Run modes:
  - `--mode http`: HTTP request benchmark (`/user/create`)
  - `--mode ws`: WS message round-trip benchmark (`/user/create`)
  - `--mode mixed`: HTTP+WS mixed benchmark on single port (default split by `--mixed-http-ratio 0.5`)
- Output:
  - JSON summary to stdout
  - Optional `--output <file>` for report persistence

Example:

```bash
build/linux-debug-local/bench/gateway_http_ws_bench --mode http --concurrency 16 --requests 1000 --warmup 100 --io-threads 4
build/linux-debug-local/bench/gateway_http_ws_bench --mode ws --concurrency 16 --requests 1000 --warmup 100 --io-threads 4
build/linux-debug-local/bench/gateway_http_ws_bench --mode mixed --concurrency 16 --requests 1000 --warmup 100 --io-threads 4 --mixed-http-ratio 0.5
```

## One-Click Script

- Script: `scripts/bench/run_http_ws_bench.sh`
- Output folder: `artifacts/bench/`
- Default profile:
  - concurrency: 16
  - requests: 1000
  - warmup: 100
  - mixed-http-ratio: 0.5

Run:

```bash
scripts/bench/run_http_ws_bench.sh build/linux-debug-local
```

## Recommended Validation Flow

1. Build `linux-debug-local`.
2. Run integration tests (if GTest is available).
3. (Optional) Run benchmark script and archive generated JSON reports.
4. Compare QPS and p95/p99 latency with previous baselines.

## Baseline Snapshot (2026-03-12)

This snapshot is generated on local WSL2 (`linux-debug-local`) with current single-port gateway implementation.

### Gateway Regression Check

Command:

```bash
build/linux-debug-local/tests/integration_tests --gtest_color=no --gtest_filter='Gateway*'
```

Result:

- 14 tests passed (GatewayIntegrationTest + GatewayAsyncIntegrationTest)
- No functional regression on HTTP route, WS upgrade/auth, async round-trip, structured errors, WS size-guard paths, and deadline cancellation propagation

### Performance Baseline (HTTP/WS)

Command:

```bash
scripts/bench/run_http_ws_bench.sh build/linux-debug-local
```

Raw outputs:

- `artifacts/bench/http-20260312-151815.json`
- `artifacts/bench/ws-20260312-151815.json`
- `artifacts/bench/mixed-20260312-151815.json`

Key metrics:

| Mode | Concurrency | Requests | io_threads | QPS | p50(us) | p95(us) | p99(us) | Errors |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| HTTP | 16 | 1000 | 4 | 2103.16 | 7247 | 10677 | 13008 | 0 |
| WS | 16 | 1000 | 4 | 2553.77 | 6036 | 7653 | 8985 | 0 |
| Mixed(total) | 16 | 1000 | 4 | 2316.90 | - | - | - | 0 |

Mixed protocol split (`--mixed-http-ratio 0.5`, HTTP=500 / WS=500):

| Mixed Protocol | QPS | p50(us) | p95(us) | p99(us) | Errors |
| --- | ---: | ---: | ---: | ---: | ---: |
| HTTP | 1159.93 | 6769 | 8352 | 10100 | 0 |
| WS | 1164.86 | 6599 | 8131 | 9284 | 0 |

### CPU/RSS Sample (same profile, `/usr/bin/time -v`)

Raw outputs:

- `artifacts/bench/http-20260312-110417-cpu.json`
- `artifacts/bench/ws-20260312-110421-cpu.json`

System sample:

| Mode | CPU% | User(s) | Sys(s) | Max RSS(KB) |
| --- | ---: | ---: | ---: | ---: |
| HTTP | 275% | 1.80 | 0.62 | 31596 |
| WS | 262% | 1.73 | 0.38 | 31004 |

Note:

- `time -v` reflects the benchmark process (embedded gateway + load client in one process) and is used as quick baseline only.
- For single-port mixed-load acceptance in P2.5, keep using dedicated run records and compare against this snapshot.

## Single-Port Mixed Benchmark Notes

`gateway_http_ws_bench` now supports `--mode mixed` and emits:

- Total mixed throughput (`success/errors/elapsed_ms/qps`)
- Protocol-level split stats under `protocols.http` and `protocols.ws`
- Request split metadata under `split` (`http_requests`, `ws_requests`)

Recommended usage for repeatable comparison:

1. Keep fixed profile (`--concurrency 16 --requests 1000 --warmup 100 --io-threads 4`).
2. Compare HTTP-only, WS-only, and mixed outputs from the same build machine.
3. Track error rate and tail latency (`p95/p99`) for each protocol in mixed mode.
