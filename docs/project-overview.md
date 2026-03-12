# kd39 项目概览

本文档面向 AI Agent 和新开发者，快速了解项目结构、技术选型、构建方式和开发约定。

## 项目定位

C++ 游戏服务与后台服务工程，面向 K8S 演进，支持多服务 gRPC 通信。

## 技术栈

| 领域 | 选型 |
|------|------|
| 语言标准 | C++20 |
| 构建系统 | CMake >= 3.24, Ninja (Windows/Linux) |
| 编译器 | MSVC cl.exe (Windows), GCC/Clang (Linux) |
| 包管理 | vcpkg Manifest Mode |
| RPC | gRPC + Protobuf |
| 日志 | spdlog |
| 配置解析 | nlohmann/json, yaml-cpp |
| HTTP/WebSocket | Boost.Beast/Asio (已接入基础监听，持续完善中) |
| 协调基础设施 | etcd (通过 FetchContent 集成 etcd-cpp-apiv3 v0.15.4) |
| 数据库 | MySQL (当前内存模拟后端) |
| 缓存 | Redis (当前内存模拟后端) |
| 消息队列 | Redis Streams (当前进程内 LocalBus 模拟) |
| 测试框架 | GTest |
| 可观测性 | OpenTelemetry + Prometheus (可选 feature) |
| 容器化 | Docker Compose (基础设施 + 应用) |
| 部署 | K8S Deployment/Service/Helm chart |

## 目录结构

```text
kd39/
├── CMakeLists.txt          # 根构建入口
├── CMakePresets.json        # 构建预设 (Ninja + cl / Ninja + gcc)
├── vcpkg.json               # vcpkg manifest 依赖声明
├── .clangd                  # clangd 编译数据库配置
├── .gitignore
│
├── api/                     # Protobuf / gRPC 接口定义
│   ├── CMakeLists.txt
│   └── proto/
│       ├── common/context.proto
│       ├── config/config_service.proto
│       ├── user/user_service.proto
│       └── game/game_service.proto
│
├── common/                  # 通用库
│   ├── include/common/
│   │   ├── context/         # RequestContext, ContextStore (thread-local)
│   │   ├── config/          # ServiceConfig, GatewayConfig, YAML 加载
│   │   ├── error/           # ErrorCode 枚举
│   │   └── log/             # spdlog 初始化
│   └── ...
│
├── framework/               # 应用框架
│   ├── include/framework/
│   │   ├── app/             # Application (信号处理, shutdown hooks)
│   │   ├── rpc/             # Client/Server Interceptor (元数据透传)
│   │   └── governance/      # 限流、熔断、重试、流量路由
│   └── ...
│
├── infrastructure/          # 基础设施适配层
│   ├── storage/
│   │   ├── mysql/           # ConnectionPool 抽象 + 内存模拟实现
│   │   └── redis/           # RedisClient 抽象 + 内存模拟实现
│   ├── mq/
│   │   └── core/            # Producer/Consumer 抽象 + LocalBus 模拟
│   ├── coordination/
│   │   ├── registry/        # ServiceRegistry/Resolver 抽象
│   │   ├── lock/            # DistributedLock 抽象
│   │   ├── election/        # LeaderElection 抽象
│   │   ├── watch/           # ConfigProvider 抽象
│   │   └── impl/etcd/       # etcd 实现 (内存模拟 SharedEtcdState)
│   └── observability/       # Tracer + MetricsRegistry 骨架
│
├── services/                # 业务服务
│   ├── config_service/      # 配置中心 (gRPC, 端口 50051)
│   ├── user_service/        # 用户服务 (gRPC, 端口 50052)
│   └── game_service/        # 游戏服务 (gRPC, 端口 50053)
│
├── gateways/
│   └── access_gateway/      # 统一入口网关 (HTTP/WS 同端口 8080)
│       ├── http/            # HttpServer (真实监听 + 测试入口复用)
│       ├── ws/              # WsServer (历史实现与测试复用)
│       ├── auth/            # AuthMiddleware (JWT + 兼容 legacy token)
│       ├── routing/         # GrpcRouter (路由、超时、重试、熔断、限流、连接复用)
│       └── context/         # Header -> RequestContext 映射
│
├── tools/
│   └── grpc_cli/            # 命令行 gRPC 测试客户端
│
├── tests/
│   ├── common/              # ErrorCode, RequestContext 单元测试
│   └── integration/         # ConfigService, UserService, Gateway, MQ 集成测试
│
├── bench/                   # Gateway HTTP/WS 基准程序
│   └── gateway_http_ws_bench.cpp
│
├── config/                  # 各服务 YAML 配置文件模板
│   ├── config_service.yaml
│   ├── user_service.yaml
│   ├── game_service.yaml
│   └── access_gateway.yaml
│
├── deploy/
│   ├── docker/
│   │   ├── docker-compose.yml  # Redis + MySQL + etcd + 应用容器
│   │   ├── Dockerfile.service  # 多阶段构建
│   │   ├── init/mysql/init.sql # MySQL 初始化表结构
│   │   └── .env
│   ├── k8s/                 # Deployment + Service YAML
│   └── helm/                # Helm chart
│
├── cmake/
│   ├── CompilerOptions.cmake   # MSVC/GCC/Clang 编译选项
│   ├── Dependencies.cmake      # find_package 统一入口
│   ├── ProtoGen.cmake          # proto -> C++ 代码生成
│   └── FetchEtcdCppApiv3.cmake # etcd-cpp-apiv3 FetchContent 集成
│
├── scripts/
│   └── build-windows.bat    # vcvarsall + cmake preset 包装脚本
│
└── docs/
    ├── README.md                    # 文档总索引
    ├── agent-quickstart.md          # 新 Agent 快速上手
    ├── build-with-vcpkg.md          # 构建说明
    ├── gateway-async-roadmap.md     # 网关异步演进路线
    ├── gateway-benchmark.md         # 网关测试与压测说明
    ├── project-overview.md          # 本文档（导航总览）
    └── services/
        ├── config-service-architecture.md
        ├── user-service-architecture.md
        ├── game-service-architecture.md
        └── access-gateway-architecture.md
```

## 架构概览

```text
H5/Admin/GameClient
        |
   AccessGateway (HTTP/WS -> gRPC 转发)
        |
   +---------+---------+---------+
   |         |         |         |
ConfigSvc  UserSvc  GameSvc  MatchSvc(规划)
   |         |         |
   +----+----+----+----+
        |         |
     MySQL     Redis      etcd       MQ(Redis Streams)
  (持久化)   (缓存)   (注册/锁/watch) (异步事件)
```

## 服务清单

| 服务 | 端口 | 状态 | 说明 |
|------|------|------|------|
| config_service | 50051 | 已实现 gRPC 接口 | GetConfig, BatchGetConfig, PublishConfig, WatchConfig |
| user_service | 50052 | 已实现 gRPC 接口 | GetUser, CreateUser |
| game_service | 50053 | 已实现 gRPC 接口 | CreateRoom, JoinRoom |
| access_gateway | 8080 | HTTP/WS 同端口 + gRPC 路由 | 支持同端口 upgrade 分流、/health /ready /metrics、/config/* /user/* /game/* 转发 |
| grpc_cli | CLI 工具 | 已实现 | 命令行调用三个服务的所有 RPC |

## 文档导航（建议先读）

| 类别 | 文档 | 说明 |
|------|------|------|
| 总览 | `project-overview.md` | 项目高层结构与服务地图（本文档） |
| 索引 | `README.md` | docs 分层索引与跳转入口 |
| 新 Agent 上手 | `agent-quickstart.md` | 3-5 分钟建立项目心智模型 |
| 构建 | `build-with-vcpkg.md` | 详细构建、工具链、可选 feature |
| 网关路线 | `gateway-async-roadmap.md` | access_gateway 异步化阶段与风险 |
| 网关验证 | `gateway-benchmark.md` | HTTP/WS 测试与基准入口 |

## 服务实现架构文档（按服务拆分）

| 服务 | 架构文档 |
|------|----------|
| config_service | `services/config-service-architecture.md` |
| user_service | `services/user-service-architecture.md` |
| game_service | `services/game-service-architecture.md` |
| access_gateway | `services/access-gateway-architecture.md` |

## 构建与运行入口（下沉后）

- 构建命令、vcpkg、可选 feature、clangd：见 `build-with-vcpkg.md`
- 网关契约、异步演进、压测与验证：见 `services/access-gateway-architecture.md`、`gateway-async-roadmap.md`、`gateway-benchmark.md`

## 服务间通信上下文

内部 gRPC 调用统一透传以下 metadata 字段：

| 字段 | 说明 |
|------|------|
| x-request-id | 请求唯一标识 |
| x-trace-id | 链路追踪 ID |
| x-user-id | 当前用户 ID |
| x-traffic-tag | 流量染色标签（灰度/AB/压测） |
| x-client-version | 客户端版本 |
| x-zone | 机房/分区标识 |

通过 `framework/rpc/` 下的 Client/Server Interceptor 自动注入和提取，业务代码通过 `common::GetCurrentRequestContext()` 读取。

## 当前状态（导航级）

- 三个 gRPC 服务（Config/User/Game）已提供完整 RPC 接口。
- `access_gateway` 已支持真实 HTTP/WS 监听、鉴权、路由、治理与基础可观测。
- MySQL/Redis/MQ/etcd 当前以内存模拟实现为主，真实 SDK 接入属于后续演进项。
- 详细实现路径、关键配置和测试入口请转到对应服务架构文档。
