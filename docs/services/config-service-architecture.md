# config_service 实现架构

## 职责边界

- 提供配置读写与变更查询能力：`GetConfig`、`BatchGetConfig`、`PublishConfig`、`WatchConfig`。
- 负责配置数据的多层存储读写路径：Redis 缓存 -> MySQL 持久层 -> 进程内内存表兜底。
- 在发布配置时向协调层和 MQ 广播变更（etcd `Put` + `config.changed` 事件）。
- 不负责鉴权与外部 HTTP 协议适配（由 `access_gateway` 负责）。

## 启动链路（main -> 依赖 -> server）

1. `main` 读取 `config/config_service.yaml`（默认）并加载 `ServiceConfig`。
2. 初始化日志（`InitLogger`）。
3. 构造依赖：
   - `ConnectionPool::Create(...)`
   - `RedisClient::Create(...)`
   - `CreateConfigProvider(...)`
   - `CreateRedisStreamsProducer(...)`
   - `CreateServiceRegistry(...)`
4. 构造 `ConfigServiceImpl` 并注册到 `grpc::ServerBuilder`。
5. `BuildAndStart` 成功后向注册中心注册服务实例，最后 `server->Wait()` 阻塞运行。

## 请求路径（RPC）

- `GetConfig`
  - 输入：`namespace_name` + `key`
  - 路径：先查 Redis，再查 MySQL，最后查内存表
  - 结果：不存在返回 `NOT_FOUND`
- `BatchGetConfig`
  - 输入：`namespace_name`
  - 路径：遍历内存表并返回当前命名空间数据
- `PublishConfig`
  - 输入：`entry(namespace/key/value/environment)`
  - 行为：写入内存表 -> Upsert MySQL -> 回写 Redis -> etcd Put -> MQ Publish
  - 结果：返回新版本号（`version`）
- `WatchConfig`
  - 输入：`namespace_name` + `since_version`
  - 行为：从内存表筛选增量并通过流式 writer 返回

## 关键配置

文件：`config/config_service.yaml`

- 网络：`bind_host`、`grpc_port`
- 数据：`mysql_*`、`redis_*`
- 协调/消息：`etcd_endpoints`、`mq_uri`
- 服务注册：`registry_ttl_seconds`
- 日志：`log_dir`、`log_max_size_mb`、`log_max_files`、`log_level`

## 依赖关系

- 上游调用方：`access_gateway`（通过 gRPC 路由进入）。
- 下游依赖：
  - MySQL 连接池抽象（当前内存模拟）
  - Redis 客户端抽象（当前内存模拟）
  - etcd ConfigProvider/ServiceRegistry（当前内存模拟）
  - MQ Producer（当前 LocalBus 模拟）

## 可观测性

- 当前主要是结构化日志（`KD39_LOG_*`）。
- 尚未在服务内独立暴露指标端点；指标聚合主要在 gateway 层进行。

## 代码入口（main/impl/config/test/CMake）

- `main`：`services/config_service/main.cpp`
- `impl`：`services/config_service/config_service_impl.h`、`services/config_service/config_service_impl.cpp`
- `config`：`config/config_service.yaml`
- `test`：`tests/integration/config_service_test.cpp`
- `CMake`：`services/config_service/CMakeLists.txt`、`tests/CMakeLists.txt`

## 测试入口与盲区

- 已有入口：
  - 集成测试目标 `integration_tests` 中的 `config_service_test.cpp`
- 主要盲区：
  - `WatchConfig` 的持续流式场景和断线重连语义覆盖不足。
  - 多副本并发发布下的版本一致性与冲突策略尚未专项验证。

## 关联文档

- 文档索引：`../README.md`
- 新 Agent 入口：`../agent-quickstart.md`
- 项目总览：`../project-overview.md`
