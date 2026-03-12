# access_gateway 实现架构

## 职责边界

- 对外提供 HTTP/WS 统一接入，并转发到内部 gRPC 服务。
- 负责鉴权、请求上下文构建、错误映射、基础治理（限流/熔断/重试）与异步 RPC 调度。
- 提供运维端点：`/health`、`/ready`、`/metrics`、`/admin/log-level`、`/admin/runtime-config`。
- 不直接承载业务数据存储，核心业务逻辑仍在 config/user/game 三个 gRPC 服务。

## 同端口现状（已落地）

- `main` 仅启动 `HttpServer`，统一监听 `http_port`。
- 同一会话入口基于 `websocket::is_upgrade(req)` 分流 HTTP 与 WS。
- WS 鉴权在握手阶段读取 `Authorization`，不再依赖消息体 `auth_token`。
- 设计与实施背景见：`access-gateway-single-port-design.md`。

## 启动链路（main -> 依赖 -> server）

1. `main` 读取 `config/access_gateway.yaml`（`LoadGatewayConfig`）。
2. 初始化日志并打印运行时参数（io 线程、重试参数）。
3. 构造核心依赖：
   - `ServiceResolver`（etcd）
   - `GrpcRouter`（目标服务 + 运行时配置）
   - `AuthMiddleware`（JWT + legacy token）
4. 构造并启动 `HttpServer`（同端口承载 HTTP+WS）。
5. 注册 `Application` shutdown hook，确保 `http.Stop()` 有序执行。

## 请求路径

### HTTP 路径

- 基础运维：
  - `GET /health`
  - `GET /ready`
  - `GET /metrics`
- 管理接口（admin）：
  - `GET/POST /admin/log-level`
  - `GET/POST /admin/runtime-config`
- 业务转发：
  - `POST /config/get`
  - `POST /config/publish`
  - `POST /user/get`
  - `POST /user/create`
  - `POST /game/create-room`
  - `POST /game/join-room`

### WebSocket 消息路径

单条消息 JSON 结构（核心字段）：

- `path`（与 HTTP 转发路径一致，如 `/user/create`）
- `body`（字符串化 JSON，作为下游 RPC 入参）
- `headers`（可选，支持 `x-request-id/x-trace-id/x-traffic-tag/x-client-version/x-zone`）

错误返回统一为结构化 JSON：`error`、`code`、`status_code`。

## 关键配置

文件：`config/access_gateway.yaml`

- 监听与线程：`bind_host`、`http_port`、`http_io_threads`
- 下游目标：`config_service_target`、`user_service_target`、`game_service_target`
- 鉴权：`jwt_secret`、`jwt_issuer`、`jwt_audience`、`allow_legacy_token`
- 路由治理：`grpc_timeout_ms`、`grpc_retry_attempts`、`grpc_retry_backoff_ms`

## 依赖关系

- 上游：客户端（H5/Admin/GameClient）通过 HTTP/WS 访问网关。
- 下游：
  - `config_service`、`user_service`、`game_service` 的 `asio-grpc` 客户端调用链
  - `ServiceResolver`（服务发现）
  - `CircuitBreaker`、`TokenBucketRateLimiter`（治理）
  - `MetricsRegistry`、`Tracer`（观测）

## 可观测性

- 路由层会记录请求总量、路由维度、状态码维度、下游服务维度指标。
- 统一输出路由日志（path/status/service/latency）。
- `/metrics` 提供 Prometheus 文本格式输出。

## 线程模型（当前实现）

- 单一 listener 采用 `1 accept io_context + N worker io_context`。
- `accept` 线程只做 `async_accept`，随后通过 round-robin 将 socket 转移到 worker。
- worker 在同一入口执行 HTTP 请求处理或 WS 会话循环（upgrade 分流）。
- 通过 `executor_work_guard` 保持 accept/worker 事件循环常驻，避免空转退出。

## 代码入口（main/impl/config/test/CMake）

- `main`：`gateways/access_gateway/main.cpp`
- `impl`：
  - `gateways/access_gateway/http/http_server.h/.cpp`
  - `gateways/access_gateway/routing/grpc_router.h/.cpp`
  - `gateways/access_gateway/auth/auth_middleware.h/.cpp`
- `config`：`config/access_gateway.yaml`
- `test`：
  - `tests/integration/gateway_test.cpp`
  - `tests/integration/gateway_async_test.cpp`
  - `bench/gateway_http_ws_bench.cpp`
  - `scripts/bench/run_http_ws_bench.sh`
  - `scripts/bench/run_ws_stability_bench.sh`
- `CMake`：`gateways/access_gateway/CMakeLists.txt`、`bench/CMakeLists.txt`、`tests/CMakeLists.txt`

## 测试入口与盲区

- 已有入口：
  - `gateway_test.cpp`（网关功能链路）
  - `gateway_async_test.cpp`（HTTP/WS 并发与结构化错误）
  - `gateway_http_ws_bench`（QPS/P50/P95/P99 基准）
- 主要盲区：
  - 已具备 WS 心跳/背压专项基线，但更大规模、长时运行场景仍需持续压测。
  - 已覆盖并发取消传播回归，但 `asio-grpc` 尾延迟波动仍需持续专项验证。

## 关联文档

- 文档索引：`../README.md`
- 新 Agent 入口：`../agent-quickstart.md`
- 项目总览：`../project-overview.md`
- 同端口改造设计：`access-gateway-single-port-design.md`
- 演进计划：`../gateway-async-roadmap.md`
- 验证与压测：`../gateway-benchmark.md`
