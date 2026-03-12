# game_service 实现架构

## 职责边界

- 提供房间生命周期中的基础能力：`CreateRoom`、`JoinRoom`。
- 管理房间状态（房间名、人数上限、成员集合）并维护进程内状态。
- 在创建房间时将房间快照写入 Redis（当前用于缓存/模拟持久化）。
- 不负责匹配、战斗、状态同步等复杂游戏逻辑。

## 启动链路（main -> 依赖 -> server）

1. `main` 读取 `config/game_service.yaml`。
2. 初始化日志。
3. 构造 Redis 客户端和 ServiceRegistry。
4. 构造 `GameServiceImpl`，注册到 gRPC server。
5. 启动后完成服务注册，`server->Wait()` 常驻运行。

## 请求路径（RPC）

- `CreateRoom`
  - 输入：`room_name`、`max_players`
  - 校验：`room_name` 不能为空，`max_players > 0`
  - 行为：生成 `room_<seq>`，写入内存房间表，并缓存至 Redis
  - 错误：参数非法返回 `INVALID_ARGUMENT`
- `JoinRoom`
  - 输入：`room_id`、`user_id`
  - 行为：查内存房间表并尝试加入成员
  - 错误：
    - 房间不存在：`NOT_FOUND`
    - 房间满员：`FAILED_PRECONDITION`

## 关键配置

文件：`config/game_service.yaml`

- 网络：`bind_host`、`grpc_port`
- 数据：`redis_*`
- 协调：`etcd_endpoints`
- 注册：`registry_ttl_seconds`
- 日志：`log_dir`、`log_max_size_mb`、`log_max_files`、`log_level`

## 依赖关系

- 上游：`access_gateway` 的 `/game/create-room`、`/game/join-room`。
- 下游：
  - Redis 客户端（房间快照写入）
  - etcd 注册中心（服务发现）
- 当前未依赖 MySQL；房间核心状态仍以内存结构为主。

## 可观测性

- 当前主要通过日志记录。
- 指标和延迟统计由 gateway 路由层统一汇总更完整。

## 代码入口（main/impl/config/test/CMake）

- `main`：`services/game_service/main.cpp`
- `impl`：`services/game_service/game_service_impl.h`、`services/game_service/game_service_impl.cpp`
- `config`：`config/game_service.yaml`
- `test`：暂无独立 `game_service` 集成测试文件（见下方盲区）
- `CMake`：`services/game_service/CMakeLists.txt`、`tests/CMakeLists.txt`

## 测试入口与盲区

- 现有覆盖：
  - `integration_tests` 链接了 `game_service_lib`，并可通过 gateway 集成路径间接覆盖部分行为。
- 主要盲区：
  - 缺少独立的 `game_service` 集成测试（Create/Join 异常路径未系统回归）。
  - 缺少并发入房、房间满员边界和多实例一致性验证。

## 关联文档

- 文档索引：`../README.md`
- 新 Agent 入口：`../agent-quickstart.md`
- 项目总览：`../project-overview.md`
