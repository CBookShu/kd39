# Agent Quickstart

目标：让新 Agent 在 3-5 分钟内建立 kd39 的基础心智模型，并能快速定位改动入口。

## 0. 先看什么（3 分钟路径）

1. 读 `README.md`，理解 docs 分层结构。
2. 读 `project-overview.md`，拿到系统边界和服务地图。
3. 根据任务域跳转到 `services/*-architecture.md`：
   - 配置链路：`services/config-service-architecture.md`
   - 用户链路：`services/user-service-architecture.md`
   - 房间/游戏链路：`services/game-service-architecture.md`
   - HTTP/WS 接入链路：`services/access-gateway-architecture.md`
   - 网关同端口改造方案：`services/access-gateway-single-port-design.md`

## 1. 代码总入口（先看这些路径）

- 构建入口：`CMakeLists.txt`
- 依赖解析：`cmake/Dependencies.cmake`
- Proto 定义：`api/proto/`
- 通用配置结构：`common/include/common/config/app_config.h`
- 配置加载实现：`common/config/app_config.cpp`
- 三个服务入口：
  - `services/config_service/main.cpp`
  - `services/user_service/main.cpp`
  - `services/game_service/main.cpp`
- 网关入口：`gateways/access_gateway/main.cpp`

## 2. 常见任务怎么定位

- **改某个 RPC 接口**：先改 `api/proto/*`，再看对应 `services/*_service/*_impl.*` 和 gateway 路由映射。
- **改网关请求路由**：看 `gateways/access_gateway/routing/grpc_router.*` 和 `http/http_server.*`（同端口 HTTP/WS）。
- **改鉴权**：看 `gateways/access_gateway/auth/auth_middleware.*`。
- **改运行时配置项**：同时改 `app_config.h`、`app_config.cpp`、`config/*.yaml`、可能还包括 `deploy/k8s/configmap.yaml`。
- **加测试/回归**：先看 `tests/integration/`，网关性能验证看 `bench/gateway_http_ws_bench.cpp` 与 `scripts/bench/run_http_ws_bench.sh`。

## 3. 关键运行参数速记

- 服务通用端口：
  - `config_service`: `50051`
  - `user_service`: `50052`
  - `game_service`: `50053`
- 网关端口：
  - HTTP/WS 统一端口: `8080`
- 网关关键配置：`config/access_gateway.yaml`
  - `http_io_threads`
  - `grpc_timeout_ms`
  - `grpc_retry_attempts`
  - `grpc_retry_backoff_ms`
  - `enable_cobalt_experimental`

## 4. 执行改动前的最小核对

1. 目标服务文档是否已阅读（`docs/services/...`）。
2. 目标配置项是否在 `app_config` 与 `config/*.yaml` 双向对齐。
3. 目标改动是否影响网关转发路径和错误语义。
4. 至少补一个对应测试入口（单测或集成测试或 bench）。

## 5. 补充文档（按需）

- 构建/工具链细节：`build-with-vcpkg.md`
- 网关异步演进计划：`gateway-async-roadmap.md`
- 网关测试与压测：`gateway-benchmark.md`
- 网关同端口实施前设计：`services/access-gateway-single-port-design.md`
