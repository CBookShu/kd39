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
| HTTP/WebSocket | Boost.Beast/Asio (规划中) |
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
│   └── access_gateway/      # 统一入口网关 (HTTP 8080, WS 8081)
│       ├── http/            # HttpServer
│       ├── ws/              # WsServer
│       ├── auth/            # AuthMiddleware (简单 token 校验)
│       ├── routing/         # GrpcRouter (HTTP path -> gRPC stub 转发)
│       └── context/         # Header -> RequestContext 映射
│
├── tools/
│   └── grpc_cli/            # 命令行 gRPC 测试客户端
│
├── tests/
│   ├── common/              # ErrorCode, RequestContext 单元测试
│   └── integration/         # ConfigService, UserService, Gateway, MQ 集成测试
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
    ├── build-with-vcpkg.md  # 构建说明
    └── project-overview.md  # 本文档
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
| access_gateway | 8080/8081 | HTTP/WS 骨架 + gRPC 路由 | 支持 /config/*, /user/*, /game/* 路径转发 |
| grpc_cli | CLI 工具 | 已实现 | 命令行调用三个服务的所有 RPC |

## 依赖管理策略

- **核心依赖**：通过 `vcpkg.json` manifest 管理 (grpc, protobuf, fmt, spdlog, nlohmann-json, yaml-cpp, boost-system, openssl)
- **etcd 客户端**：通过 CMake `FetchContent` 拉取 `etcd-cpp-apiv3 v0.15.4`，不走 vcpkg
- **可选 feature**：`tests` (gtest), `observability` (opentelemetry-cpp, prometheus-cpp), `gateway` (boost-asio, boost-beast)
- **protoc / grpc_cpp_plugin**：通过 vcpkg host 依赖提供

## 构建方式

### Windows

必须先激活 MSVC 开发环境。推荐使用包装脚本：

```bat
scripts\build-windows.bat windows-debug
scripts\build-windows.bat windows-release
```

或手动打开 "x64 Native Tools Command Prompt for VS 2022" 后运行：

```bat
cmake --preset windows-debug
cmake --build --preset build-windows-debug
```

### Linux

```bash
cmake --preset linux-debug
cmake --build --preset build-linux-debug
```

### 启用可选功能

```bat
:: etcd 客户端
scripts\build-windows.bat windows-debug -DKD39_ENABLE_ETCD=ON

:: 可观测性
scripts\build-windows.bat windows-debug -DVCPKG_MANIFEST_FEATURES="observability" -DKD39_ENABLE_OBSERVABILITY=ON
```

## CMake 选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| BUILD_TESTS | ON | 编译单元测试和集成测试 |
| KD39_ENABLE_TESTS | ON | 解析 GTest 依赖 |
| KD39_ENABLE_ETCD | OFF | 启用 etcd-cpp-apiv3 FetchContent |
| KD39_ETCD_CORE_ONLY | ON | 只编译 etcd 同步运行时 |
| KD39_ENABLE_OBSERVABILITY | OFF | 启用 OpenTelemetry / Prometheus |
| KD39_ENABLE_GATEWAY | OFF | 启用完整 Boost 网络栈 |
| KD39_ETCD_CPP_APIV3_TAG | v0.15.4 | FetchContent 锁定的 etcd 版本 |

## CMake 目标命名

| 类型 | 示例 |
|------|------|
| 公共库 | common_base |
| 框架库 | framework_app, framework_rpc, framework_governance |
| 基础设施库 | storage_mysql, storage_redis, mq_core, coord_registry, coord_etcd, observability_core |
| 服务库 | config_service_lib, user_service_lib, game_service_lib |
| 网关库 | access_gateway_lib |
| 可执行文件 | config_service, user_service, game_service, access_gateway, grpc_cli |
| Proto 生成库 | api_proto |

## 当前实现状态

### 已完整实现

- 工程骨架、CMake 组织、Proto 定义
- vcpkg manifest + FetchContent(etcd) 依赖管理
- 三个 gRPC 服务 (ConfigService, UserService, GameService) 继承生成基类并实现所有 RPC
- access_gateway 的 HTTP/WS 骨架 + gRPC 路由转发 + 简单鉴权
- Client/Server Interceptor 元数据透传 (thread-local RequestContext)
- grpc_cli 命令行测试客户端
- Docker Compose 基础设施 (Redis + MySQL + etcd)
- K8S Deployment/Service/ConfigMap/Secret + Helm chart
- 服务治理骨架 (限流、熔断、重试、流量路由)
- 可观测性骨架 (Tracer, MetricsRegistry)
- YAML 配置文件加载
- 单元测试 + 集成测试骨架

### 使用内存模拟后端

- MySQL ConnectionPool (SharedState 内存表)
- Redis Client (内存 KV + Streams)
- MQ Producer/Consumer (进程内 LocalBus)
- etcd coordination (SharedEtcdState 内存)

### 后续接入真实 SDK 时需要替换

- MySQL: 接入 mysqlclient 或 mysql-connector-c++
- Redis: 接入 redis-plus-plus
- MQ: 接入 redis-plus-plus XADD/XREADGROUP
- etcd: 当前 FetchContent 已就绪，需接入 etcd-cpp-apiv3 的 SyncClient API
- HTTP/WS: 接入 Boost.Beast 实现真实网络监听

## Docker 本地开发

```bash
cd deploy/docker
docker compose up -d                    # 拉起 Redis + MySQL + etcd
docker compose --profile app up -d      # 同时拉起应用容器
docker compose down -v                  # 停止并清除数据
```

连接地址：

- Redis: 127.0.0.1:6379
- MySQL: 127.0.0.1:3306, root/kd39dev, db=kd39
- etcd: 127.0.0.1:2379

## 命名约定

- 服务：`<domain>_service` (config_service, user_service, game_service)
- 网关：`<role>_gateway` (access_gateway)
- 库：按能力命名 (registry, lock, election, storage_mysql, mq_core)
- 避免使用：base_server, manager, helper, engine, kernel 等过宽名称
- 顶层目录：api, common, framework, infrastructure, services, gateways, deploy, tools, tests

## clangd 集成

项目根目录的 `.clangd` 指向 `build/windows-debug/compile_commands.json`。

构建成功后 clangd 自动获取 include 路径、编译选项和宏定义。

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

## Proto 接口摘要

### ConfigService (config/config_service.proto)

- `GetConfig(namespace, key) -> ConfigEntry`
- `BatchGetConfig(namespace) -> [ConfigEntry]`
- `PublishConfig(ConfigEntry) -> version`
- `WatchConfig(namespace, since_version) -> stream [ConfigEntry]`

### UserService (user/user_service.proto)

- `GetUser(user_id) -> UserProfile`
- `CreateUser(nickname) -> UserProfile`

### GameService (game/game_service.proto)

- `CreateRoom(room_name, max_players) -> room_id`
- `JoinRoom(room_id, user_id) -> success`

## 抽象接口清单

| 接口 | 头文件 | 当前实现 |
|------|--------|----------|
| ServiceRegistry | infrastructure/coordination/service_registry.h | EtcdServiceRegistry (内存) |
| ServiceResolver | infrastructure/coordination/service_registry.h | EtcdServiceResolver (内存) |
| DistributedLock | infrastructure/coordination/distributed_lock.h | EtcdDistributedLock (内存) |
| LeaderElection | infrastructure/coordination/leader_election.h | EtcdLeaderElection (内存) |
| ConfigProvider | infrastructure/coordination/config_watcher.h | EtcdConfigProvider (内存) |
| ConnectionPool | infrastructure/storage/mysql/connection_pool.h | ConnectionPoolImpl (内存表) |
| RedisClient | infrastructure/storage/redis/redis_client.h | RedisClientImpl (内存 KV) |
| Producer | infrastructure/mq/producer.h | RedisStreamsProducer (LocalBus) |
| Consumer | infrastructure/mq/consumer.h | RedisStreamsConsumer (LocalBus) |
