#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build/linux-debug-local}"
BENCH_BIN="$BUILD_DIR/bench/gateway_http_ws_bench"
OUT_DIR="$ROOT_DIR/artifacts/bench"

mkdir -p "$OUT_DIR"

if [[ ! -x "$BENCH_BIN" ]]; then
  echo "benchmark binary not found: $BENCH_BIN"
  echo "build first: cmake --build \"$BUILD_DIR\" -j4"
  exit 1
fi

TS="$(date +%Y%m%d-%H%M%S)"
HEARTBEAT_OUT="$OUT_DIR/ws-heartbeat-$TS.json"
BACKPRESSURE_OUT="$OUT_DIR/ws-backpressure-$TS.json"

echo "[ws-stability] heartbeat profile -> $HEARTBEAT_OUT"
"$BENCH_BIN" --mode ws --concurrency 8 --requests 400 --warmup 50 --io-threads 4 --ws-message-interval-ms 20 --output "$HEARTBEAT_OUT"

echo "[ws-stability] backpressure profile -> $BACKPRESSURE_OUT"
"$BENCH_BIN" --mode ws --concurrency 16 --requests 1000 --warmup 100 --io-threads 4 --ws-read-delay-ms 5 --output "$BACKPRESSURE_OUT"

echo "[ws-stability] done"
