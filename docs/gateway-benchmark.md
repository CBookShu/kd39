# Gateway Test And Benchmark

This document describes the regression tests and benchmark entry points for `access_gateway` HTTP/WS runtime validation.

Related docs:

- `README.md`
- `agent-quickstart.md`
- `services/access-gateway-architecture.md`
- `gateway-async-roadmap.md`

## Integration Tests

- File: `tests/integration/gateway_async_test.cpp`
- Scope:
  - Real HTTP listener with concurrent requests.
  - Real WS listener with structured error response and normal route round-trip.
- Build note:
  - Requires GTest in the current build profile.
  - Existing CI/build profile may skip tests when GTest is unavailable.

## Benchmark Binary

- Target: `gateway_http_ws_bench`
- Source: `bench/gateway_http_ws_bench.cpp`
- Run modes:
  - `--mode http`: HTTP request benchmark (`/user/create`)
  - `--mode ws`: WS message round-trip benchmark (`/user/create`)
- Output:
  - JSON summary to stdout
  - Optional `--output <file>` for report persistence

Example:

```bash
build/linux-debug-local/bench/gateway_http_ws_bench --mode http --concurrency 16 --requests 1000 --warmup 100 --io-threads 4
build/linux-debug-local/bench/gateway_http_ws_bench --mode ws --concurrency 16 --requests 1000 --warmup 100 --io-threads 4
```

## One-Click Script

- Script: `scripts/bench/run_http_ws_bench.sh`
- Output folder: `artifacts/bench/`
- Default profile:
  - concurrency: 16
  - requests: 1000
  - warmup: 100

Run:

```bash
scripts/bench/run_http_ws_bench.sh build/linux-debug-local
```

## Recommended Validation Flow

1. Build `linux-debug-local`.
2. Run integration tests (if GTest is available).
3. Run benchmark script and archive generated JSON reports.
4. Compare QPS and p95/p99 latency with previous baselines.
