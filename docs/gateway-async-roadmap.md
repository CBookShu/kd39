# Gateway Async Roadmap

本文档定义 access_gateway 的下一阶段演进路线，目标是逐步统一到 Asio 调度体系，并以协程模型提升并发能力。

前置阅读：

- `README.md`
- `agent-quickstart.md`
- `services/access-gateway-architecture.md`
- `services/access-gateway-single-port-design.md`

## 总目标

- 保持现有功能与契约稳定（路由、鉴权、运维接口、指标不回退）。
- 逐步引入 `Boost.Asio + Boost.Cobalt` 协程组织模型。
- 在开发阶段直接切换到 `asio-grpc`，实现 I/O 与 RPC 调度统一。

## 全量 TODO（按优先级）

### P0 基线与准备

- [x] 新增异步化路线与任务清单（本文件）
- [x] 补充异步化配置项（io 线程数）
- [x] 输出基线性能数据（当前单端口实现 QPS/P95/P99/CPU）
- [x] 固化回归测试集合（HTTP/WS/路由/鉴权/管理端点）
- [x] 移除 `enable_asio_grpc_experimental` 配置开关，减少重构路径分叉

#### P0 当前进度

- [x] 已新增并通过 Gateway 回归用例，覆盖 `/health`、`/ready`、`/metrics`、`/admin/log-level`、`/admin/runtime-config`
- [x] 已覆盖 WS 握手鉴权失败（缺失 `Authorization` 返回 401）与已有 WS 结构化错误路径
- [x] `GatewayIntegrationTest + GatewayAsyncIntegrationTest` 在本地 `--gtest_filter='Gateway*'` 全部通过

### P1 Asio+Cobalt 第一阶段（先做）

- [x] 抽离 HTTP 会话生命周期为可协程化 session
- [x] `accept/read/write` 改为异步链路，避免阻塞式串行处理
- [x] 保持现有 `HandleRequest` 业务入口不变（先适配再重构）
- [x] 增加连接超时、读写超时、异常关闭处理
- [x] 补充并发连接压测脚本和验证报告

#### Phase-1 分解步骤（执行顺序）

1. 在 `HttpServer` 增加运行时选项与迁移开关（不改变默认行为）
2. 抽离 `HttpSession` 基础骨架（读写、请求解析、响应发送）
3. 引入 `boost::asio::awaitable` 协程链路，保持业务处理函数复用
4. 再切换为 `Boost.Cobalt` 任务模型（保持接口兼容）
5. 使用基线压测对比同步模型，评估是否继续扩大改造范围

#### Phase-1 主要风险

- 同步 gRPC 调用会阻塞协程执行线程，需配套线程池桥接或后续异步化
- 连接生命周期、异常关闭和取消传播处理不完善会引发资源泄漏
- 先改 I/O 层后改业务层，必须保障协议与错误语义不回退

#### Phase-1 当前进度

- [x] HTTP `accept/read/write` 已迁移为协程会话模型
- [x] 已切换到 `Boost.Cobalt task + spawn` 调度模型
- [x] 已通过本地构建验证（无编译错误）
- [x] 已补充基线压测快照与验证记录（见 `gateway-benchmark.md`）

### P2 WS 协程化

- [x] 抽离 WS 会话对象并改为异步协程循环
- [x] 统一 WS 上下文注入和错误返回格式
- [x] 增加心跳、空闲断连、消息大小限制与背压策略

#### Phase-2 当前进度

- [x] WS `accept/read/write` 已迁移为 `Boost.Cobalt task + spawn`
- [x] WS 错误返回已统一为结构化 JSON（`error`/`code`/`status_code`）
- [x] 已启用 idle timeout + keepalive ping、消息大小限制与写缓冲策略

### P2.5 同端口收敛（核心已落地）

- [x] 输出同端口改造设计文档（`access-gateway-single-port-design.md`）
- [x] 将 HTTP/WS 收敛为单端口统一监听
- [x] 在统一入口使用 `websocket::is_upgrade(req)` 分流协议
- [x] WS 鉴权切换为握手 `Authorization`（不保留消息体 `auth_token`）
- [x] （可选）输出同端口下混合流量性能对比（QPS/P95/P99/错误率）

### P3 路由层异步化（全量切换前准备）

- [x] 统一路由调用入口的异步接口抽象
- [x] 下游同步 gRPC 调用通过专用线程池桥接（过渡）
- [x] 增加取消传播与超时统一治理
- [x] 固化异步错误语义回归（HTTP/WS 状态码与错误体保持一致）

#### Phase-3 当前进度

- [x] `GrpcRouter` 已提供 `RouteAsync/RouteWithStatusAsync`，HTTP/WS 会话改为协程 `co_await` 调用
- [x] 路由层已引入独立 gRPC 执行上下文与 worker 线程，避免占用 HTTP/WS worker
- [x] 超时、重试、退避与错误码映射语义保持兼容（Gateway 回归用例通过）

### P4 全量切换到 asio-grpc（开发阶段直切）

- [x] 为 `/config/*`、`/user/*`、`/game/*` 全路由接入 `asio-grpc` 客户端调用链
- [x] 移除同步 gRPC 路径与过渡桥接实现
- [x] 对齐重试/超时/取消传播语义，确保与当前协议契约一致
- [x] 通过 `Gateway*` 回归测试并补充关键路径用例

#### Phase-4 当前进度

- [x] 已引入 `asio-grpc` 依赖并在 gateway 构建链路启用
- [x] 全路由从同步 Stub 调用切换为 `agrpc::ClientRPC` 协程调用
- [x] `integration_tests --gtest_filter='Gateway*'` 持续通过（15/15）

### P5 稳定化与优化

- [x] 统一 executor/调度策略与线程模型
- [x] 仅对热点路径做细粒度锁优化或无锁改造（以 profiling 结果驱动）
- [x] 视需要恢复混合负载基准，用于长期性能回归

#### Phase-5 当前进度

- [x] HTTP/WS 与 gRPC 调用已解耦为双执行上下文（网络会话与 RPC 调度分层）
- [x] 当前未发现需要立即落地的锁热点改造点，保持“以 profiling 驱动优化”策略
- [x] 混合负载基准保留为长期回归项，不再作为当前开发阶段的发布门禁

## 实施原则

- 小步迭代：每次只改一个层面（HTTP、WS、RPC）。
- 接口稳定：先保持外部协议不变，再优化内部模型。
- 数据驱动：任何复杂优化都必须有压测/Profiling 依据。
- 可回滚：每阶段都保留稳定回退路径。
