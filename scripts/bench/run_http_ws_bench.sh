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
HTTP_OUT="$OUT_DIR/http-$TS.json"
WS_OUT="$OUT_DIR/ws-$TS.json"
MIXED_OUT="$OUT_DIR/mixed-$TS.json"

echo "[bench] http -> $HTTP_OUT"
"$BENCH_BIN" --mode http --concurrency 16 --requests 1000 --warmup 100 --io-threads 4 --output "$HTTP_OUT"

echo "[bench] ws -> $WS_OUT"
"$BENCH_BIN" --mode ws --concurrency 16 --requests 1000 --warmup 100 --io-threads 4 --output "$WS_OUT"

echo "[bench] mixed -> $MIXED_OUT"
"$BENCH_BIN" --mode mixed --concurrency 16 --requests 1000 --warmup 100 --io-threads 4 --mixed-http-ratio 0.5 --output "$MIXED_OUT"

echo "[bench] done"
