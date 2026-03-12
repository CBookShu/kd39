# Docs Index

本文档是 `docs/` 的分层索引，面向新 Agent 与新开发者快速导航。

## 1) Overview

- `project-overview.md`：项目定位、技术栈、目录和服务地图（导航型总览）。
- `agent-quickstart.md`：新 Agent 首读入口（建议阅读顺序 + 常用定位路径）。

## 2) Onboarding

- `build-with-vcpkg.md`：环境、构建、可选 feature、host tools、clangd。

## 3) Service-Specific

- `services/config-service-architecture.md`
- `services/user-service-architecture.md`
- `services/game-service-architecture.md`
- `services/access-gateway-architecture.md`
- `services/access-gateway-single-port-design.md`（同端口改造设计，实施前文档）

每个服务文档均包含：职责边界、启动链路、请求路径、关键配置、依赖关系、可观测性、测试入口与盲区。

## 4) Runbook / Roadmap

- `gateway-async-roadmap.md`：网关异步化分阶段计划与风险。

## 5) Benchmark / Quality

- `gateway-benchmark.md`：网关测试与压测入口、脚本、结果落盘规范。

## 推荐阅读路径（新 Agent）

1. `agent-quickstart.md`
2. `project-overview.md`
3. 进入目标服务的 `services/*-architecture.md`
   - 若目标是网关同端口改造，继续读 `services/access-gateway-single-port-design.md`
4. 涉及构建/运行时，再读 `build-with-vcpkg.md`
5. 涉及网关性能/演进时，再读 `gateway-async-roadmap.md` 和 `gateway-benchmark.md`
