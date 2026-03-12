#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <vector>

#include <boost/asio.hpp>
#include <boost/cobalt/task.hpp>

#include "access_gateway/auth/auth_middleware.h"
#include "access_gateway/http/http_server.h"
#include "access_gateway/routing/grpc_router.h"

namespace kd39::gateways::access {

class WsServer {
public:
    WsServer(const std::string& address,
             uint16_t port,
             std::shared_ptr<GrpcRouter> router,
             std::shared_ptr<AuthMiddleware> auth,
             ServerRuntimeOptions runtime_options = {});
    ~WsServer();

    void Start();
    void Stop();
    uint16_t bound_port() const { return port_; }

    std::string HandleMessageForTesting(const std::string& payload,
                                        const std::string& auth_token);

private:
    using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

    boost::cobalt::task<void> AcceptLoop();
    boost::cobalt::task<void> HandleSession(boost::asio::ip::tcp::socket socket);

    std::string address_;
    uint16_t port_;
    std::shared_ptr<GrpcRouter> router_;
    std::shared_ptr<AuthMiddleware> auth_;
    ServerRuntimeOptions runtime_options_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> request_seq_{1};
    std::atomic<std::size_t> next_worker_index_{0};
    std::thread accept_thread_;
    std::vector<std::thread> worker_threads_;
    std::unique_ptr<boost::asio::io_context> accept_io_context_;
    std::unique_ptr<WorkGuard> accept_work_guard_;
    std::vector<std::unique_ptr<boost::asio::io_context>> worker_io_contexts_;
    std::vector<std::unique_ptr<WorkGuard>> worker_work_guards_;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor_;
};

}  // namespace kd39::gateways::access
