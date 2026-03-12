# access_gateway 实现架构

## 职责边界

- 对外提供 HTTP/WS 统一接入，并转发到内部 gRPC 服务。
- 负责鉴权、请求上下文构建、错误映射、基础治理（限流/熔断/重试）。
- 提供运维端点：`/health`、`/ready`、`/metrics`、`/admin/log-level`、`/admin/runtime-config`。
- 不直接承载业务数据存储，核心业务逻辑仍在 config/user/game 三个 gRPC 服务。

## 启动链路（main -> 依赖 -> server）

1. `main` 读取 `config/access_gateway.yaml`（`LoadGatewayConfig`）。
2. 初始化日志并打印运行时开关（io 线程、cobalt/asio-grpc 实验开关、重试参数）。
3. 构造核心依赖：
   - `ServiceResolver`（etcd）
   - `GrpcRouter`（目标服务 + 运行时配置）
   - `AuthMiddleware`（JWT + legacy token）
4. 构造并启动 `HttpServer` 与 `WsServer`。
5. 注册 `Application` shutdown hook，确保 `ws.Stop()`/`http.Stop()` 有序执行。

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

- `auth_token`（可选；未在 upgrade 阶段读取时可放在消息体内）
- `path`（与 HTTP 转发路径一致，如 `/user/create`）
- `body`（字符串化 JSON，作为下游 RPC 入参）
- `headers`（可选，支持 `x-request-id/x-trace-id/x-traffic-tag/x-client-version/x-zone`）

错误返回统一为结构化 JSON：`error`、`code`、`status_code`。

## 关键配置

文件：`config/access_gateway.yaml`

- 监听与线程：`bind_host`、`http_port`、`ws_port`、`http_io_threads`、`ws_io_threads`
- 下游目标：`config_service_target`、`user_service_target`、`game_service_target`
- 鉴权：`jwt_secret`、`jwt_issuer`、`jwt_audience`、`allow_legacy_token`
- 路由治理：`grpc_timeout_ms`、`grpc_retry_attempts`、`grpc_retry_backoff_ms`
- 实验开关：`enable_cobalt_experimental`、`enable_asio_grpc_experimental`

## 依赖关系

- 上游：客户端（H5/Admin/GameClient）通过 HTTP/WS 访问网关。
- 下游：
  - `config_service`、`user_service`、`game_service` 的 gRPC Stub/Channel
  - `ServiceResolver`（服务发现）
  - `CircuitBreaker`、`TokenBucketRateLimiter`（治理）
  - `MetricsRegistry`、`Tracer`（观测）

## 可观测性

- 路由层会记录请求总量、路由维度、状态码维度、下游服务维度指标。
- 统一输出路由日志（path/status/service/latency）。
- `/metrics` 提供 Prometheus 文本格式输出。

## 线程模型（当前实现）

- HTTP 与 WS 都采用 `1 accept io_context + N worker io_context`。
- `accept` 线程只做 `async_accept`，随后通过 round-robin 将 socket 转移到 worker。
- worker 执行协程化会话（`Boost.Cobalt task + spawn`）完成 read/write。
- 通过 `executor_work_guard` 保持 accept/worker 事件循环常驻，避免空转退出。

## 代码入口（main/impl/config/test/CMake）

- `main`：`gateways/access_gateway/main.cpp`
- `impl`：
  - `gateways/access_gateway/http/http_server.h/.cpp`
  - `gateways/access_gateway/ws/ws_server.h/.cpp`
  - `gateways/access_gateway/routing/grpc_router.h/.cpp`
  - `gateways/access_gateway/auth/auth_middleware.h/.cpp`
- `config`：`config/access_gateway.yaml`
- `test`：
  - `tests/integration/gateway_test.cpp`
  - `tests/integration/gateway_async_test.cpp`
  - `bench/gateway_http_ws_bench.cpp`
  - `scripts/bench/run_http_ws_bench.sh`
- `CMake`：`gateways/access_gateway/CMakeLists.txt`、`bench/CMakeLists.txt`、`tests/CMakeLists.txt`

## 测试入口与盲区

- 已有入口：
  - `gateway_test.cpp`（网关功能链路）
  - `gateway_async_test.cpp`（HTTP/WS 并发与结构化错误）
  - `gateway_http_ws_bench`（QPS/P50/P95/P99 基准）
- 主要盲区：
  - WS 长连接下的大规模心跳与背压场景仍需专项压测。
  - `asio-grpc` 尚未试点，RPC 仍是同步调用 + 重试治理模型。

## 关联文档

- 文档索引：`../README.md`
- 新 Agent 入口：`../agent-quickstart.md`
- 项目总览：`../project-overview.md`
- 演进计划：`../gateway-async-roadmap.md`
- 验证与压测：`../gateway-benchmark.md`
