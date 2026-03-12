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
- 评估并分阶段引入 `asio-grpc`，最终实现 I/O 与 RPC 调度统一。

## 全量 TODO（按优先级）

### P0 基线与准备

- [x] 新增异步化路线与任务清单（本文件）
- [x] 补充异步化配置项（io 线程数、cobalt/asio-grpc 实验开关）
- [ ] 输出基线性能数据（当前同步模型 QPS/P95/P99/CPU）
- [ ] 固化回归测试集合（HTTP/WS/路由/鉴权/管理端点）

### P1 Asio+Cobalt 第一阶段（先做）

- [x] 抽离 HTTP 会话生命周期为可协程化 session
- [x] `accept/read/write` 改为异步链路，避免阻塞式串行处理
- [x] 保持现有 `HandleRequest` 业务入口不变（先适配再重构）
- [x] 增加连接超时、读写超时、异常关闭处理
- [ ] 补充并发连接压测脚本和验证报告

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
- [ ] 输出同端口下混合流量性能对比（QPS/P95/P99/错误率）

### P3 路由层异步化（不引入 asio-grpc 前）

- [ ] 统一路由调用入口的异步接口抽象
- [ ] 下游同步 gRPC 调用先通过专用线程池桥接
- [ ] 增加取消传播与超时统一治理

### P4 asio-grpc 试点

- [ ] 选择单一路径试点（建议 `/user/get`）
- [ ] 接入 `asio-grpc` 的客户端调用链并保持现有契约
- [ ] 对比同步桥接方案性能与复杂度（含维护成本）
- [ ] 形成“是否全量迁移”的决策结论

### P5 全量迁移与优化

- [ ] 按服务路径分批迁移到 `asio-grpc`
- [ ] 统一 executor/调度策略与线程模型
- [ ] 仅对热点路径做细粒度锁优化或无锁改造（以 profiling 结果驱动）

## 实施原则

- 小步迭代：每次只改一个层面（HTTP、WS、RPC）。
- 接口稳定：先保持外部协议不变，再优化内部模型。
- 数据驱动：任何复杂优化都必须有压测/Profiling 依据。
- 可回滚：每阶段都保留稳定回退路径。
