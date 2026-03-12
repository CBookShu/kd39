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

## Single-Port Migration Benchmark Guidance

Current benchmark entry is based on separate HTTP and WS runs. For the single-port migration plan, add a mixed-traffic comparison with the following baseline method:

1. Keep current outputs as baseline:
   - HTTP-only (`--mode http`)
   - WS-only (`--mode ws`)
2. During single-port implementation phase, add a mixed profile:
   - HTTP short requests and WS long sessions at the same time
   - Same hardware, same build, fixed concurrency budget
3. Record and compare:
   - QPS (HTTP path)
   - WS message round-trip latency (p50/p95/p99)
   - Error rate split by protocol
   - Tail latency regression under mixed load

Recommended acceptance gate for rollout:

- No functional regressions in integration tests
- Mixed-load p99 does not regress beyond agreed threshold
- Error rate remains stable vs. dual-port baseline
