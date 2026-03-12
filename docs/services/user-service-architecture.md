# user_service 实现架构

## 职责边界

- 提供用户读取与创建能力：`GetUser`、`CreateUser`。
- 维护用户资料在 Redis/MySQL/内存表之间的读写一致路径。
- 订阅 `config.changed` 事件用于接收配置变更通知（目前只记录日志）。
- 不负责鉴权策略与跨服务路由（由 gateway 与中间件承担）。

## 启动链路（main -> 依赖 -> server）

1. `main` 读取 `config/user_service.yaml` 并加载 `ServiceConfig`。
2. 初始化日志。
3. 构造基础依赖：
   - MySQL 连接池
   - Redis 客户端
   - ServiceRegistry
   - MQ Consumer（订阅 `config.changed`）
4. 构造 `UserServiceImpl` 并注册到 gRPC server。
5. 启动后进行服务注册，随后 `server->Wait()`；退出前停止 consumer。

## 请求路径（RPC）

- `GetUser`
  - 输入：`user_id`
  - 路径：Redis -> MySQL -> 内存表兜底
  - 结果：未命中返回 `NOT_FOUND`
- `CreateUser`
  - 输入：`nickname`
  - 行为：生成 `user_<seq>`，填充默认字段并写入内存
  - 持久化：Upsert MySQL + 回写 Redis
  - 输出：`UserProfile`

## 关键配置

文件：`config/user_service.yaml`

- 网络：`bind_host`、`grpc_port`
- 数据：`mysql_*`、`redis_*`
- 协调/消息：`etcd_endpoints`、`mq_uri`
- 服务注册：`registry_ttl_seconds`
- 日志：`log_dir`、`log_max_size_mb`、`log_max_files`、`log_level`

## 依赖关系

- 上游：`access_gateway` 调用 `/user/get`、`/user/create` 时路由到本服务。
- 下游：MySQL/Redis 抽象层、etcd 注册中心、MQ consumer。
- 横向关联：监听 `config.changed` 事件（当前未触发动态行为，仅日志记录）。

## 可观测性

- 以服务日志为主。
- 请求级指标与 route 维度指标主要在 `access_gateway` 统计。

## 代码入口（main/impl/config/test/CMake）

- `main`：`services/user_service/main.cpp`
- `impl`：`services/user_service/user_service_impl.h`、`services/user_service/user_service_impl.cpp`
- `config`：`config/user_service.yaml`
- `test`：`tests/integration/user_service_test.cpp`
- `CMake`：`services/user_service/CMakeLists.txt`、`tests/CMakeLists.txt`

## 测试入口与盲区

- 已有入口：
  - 集成测试 `user_service_test.cpp`
- 主要盲区：
  - `config.changed` 消息消费后的行为还未做功能化断言（仅日志）。
  - 高并发 `CreateUser` 下的唯一性、顺序性和缓存一致性未专项压测。

## 关联文档

- 文档索引：`../README.md`
- 新 Agent 入口：`../agent-quickstart.md`
- 项目总览：`../project-overview.md`
