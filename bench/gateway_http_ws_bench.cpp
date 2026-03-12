#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include "access_gateway/auth/auth_middleware.h"
#include "access_gateway/http/http_server.h"
#include "access_gateway/routing/grpc_router.h"
#include "common/log/logger.h"
#include "infrastructure/storage/mysql/connection_pool.h"
#include "infrastructure/storage/redis/redis_client.h"
#include "user_service_impl.h"

namespace {
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

struct BenchConfig {
    std::string mode = "http";
    int concurrency = 32;
    int requests = 2000;
    int warmup = 200;
    int io_threads = 4;
    double mixed_http_ratio = 0.5;
    std::string output_file;
};

struct BenchStats {
    int success = 0;
    int errors = 0;
    double elapsed_ms = 0.0;
    double qps = 0.0;
    std::uint64_t p50_us = 0;
    std::uint64_t p95_us = 0;
    std::uint64_t p99_us = 0;
};

bool ParseArgs(int argc, char** argv, BenchConfig* cfg) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto need_value = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << name << "\n";
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--mode") {
            const char* v = need_value("--mode");
            if (!v) return false;
            cfg->mode = v;
        } else if (arg == "--concurrency") {
            const char* v = need_value("--concurrency");
            if (!v) return false;
            cfg->concurrency = std::max(1, std::stoi(v));
        } else if (arg == "--requests") {
            const char* v = need_value("--requests");
            if (!v) return false;
            cfg->requests = std::max(1, std::stoi(v));
        } else if (arg == "--warmup") {
            const char* v = need_value("--warmup");
            if (!v) return false;
            cfg->warmup = std::max(0, std::stoi(v));
        } else if (arg == "--io-threads") {
            const char* v = need_value("--io-threads");
            if (!v) return false;
            cfg->io_threads = std::max(1, std::stoi(v));
        } else if (arg == "--mixed-http-ratio") {
            const char* v = need_value("--mixed-http-ratio");
            if (!v) return false;
            cfg->mixed_http_ratio = std::clamp(std::stod(v), 0.0, 1.0);
        } else if (arg == "--output") {
            const char* v = need_value("--output");
            if (!v) return false;
            cfg->output_file = v;
        } else if (arg == "--help" || arg == "-h") {
            return false;
        } else {
            std::cerr << "unknown arg: " << arg << "\n";
            return false;
        }
    }
    return cfg->mode == "http" || cfg->mode == "ws" || cfg->mode == "mixed";
}

std::uint64_t Percentile(std::vector<std::uint64_t> values, double pct) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    const auto idx = static_cast<std::size_t>(std::clamp(pct, 0.0, 1.0) * static_cast<double>(values.size() - 1));
    return values[idx];
}

bool HttpCreateUser(const std::string& host, const std::string& port, int seq) {
    try {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);
        auto endpoints = resolver.resolve(host, port);
        stream.connect(endpoints);

        http::request<http::string_body> req{http::verb::post, "/user/create", 11};
        req.set(http::field::host, host);
        req.set(http::field::authorization, "Bearer user:bench-user");
        req.body() = nlohmann::json{{"nickname", "bench-http-" + std::to_string(seq)}}.dump();
        req.prepare_payload();
        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> resp;
        http::read(stream, buffer, resp);
        auto json = nlohmann::json::parse(resp.body(), nullptr, false);
        try {
            stream.socket().shutdown(tcp::socket::shutdown_both);
        } catch (...) {
        }
        return resp.result() == http::status::ok && !json.is_discarded() && json.contains("user_id");
    } catch (...) {
        return false;
    }
}

void WarmupHttp(const std::string& host, const std::string& port, int warmup_requests) {
    for (int i = 0; i < warmup_requests; ++i) {
        static_cast<void>(HttpCreateUser(host, port, i));
    }
}

void WarmupWs(const std::string& host, const std::string& port, int warmup_requests) {
    try {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        websocket::stream<tcp::socket> ws(ioc);
        ws.set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
            req.set(http::field::authorization, "Bearer user:bench-user");
        }));
        auto endpoints = resolver.resolve(host, port);
        net::connect(ws.next_layer(), endpoints);
        ws.handshake(host, "/");
        for (int i = 0; i < warmup_requests; ++i) {
            const auto payload = nlohmann::json{
                {"path", "/user/create"},
                {"body", nlohmann::json{{"nickname", "bench-ws-warmup-" + std::to_string(i)}}.dump()},
                {"headers", nlohmann::json{{"x-request-id", "warmup-" + std::to_string(i)}}}}
                                     .dump();
            ws.write(net::buffer(payload));
            beast::flat_buffer buffer;
            ws.read(buffer);
        }
        ws.close(websocket::close_code::normal);
    } catch (...) {
    }
}

BenchStats RunHttpBench(const BenchConfig& cfg,
                        const std::string& host,
                        const std::string& port,
                        bool do_warmup = true) {
    if (do_warmup && cfg.warmup > 0) {
        WarmupHttp(host, port, cfg.warmup);
    }
    std::atomic<int> index{0};
    std::atomic<int> success{0};
    std::atomic<int> errors{0};
    std::vector<std::uint64_t> latencies(static_cast<std::size_t>(cfg.requests));

    auto begin = std::chrono::steady_clock::now();
    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(cfg.concurrency));
    for (int i = 0; i < cfg.concurrency; ++i) {
        workers.emplace_back([&] {
            while (true) {
                const int req_id = index.fetch_add(1);
                if (req_id >= cfg.requests) break;
                const auto start = std::chrono::steady_clock::now();
                const bool ok = HttpCreateUser(host, port, req_id);
                const auto end = std::chrono::steady_clock::now();
                latencies[static_cast<std::size_t>(req_id)] =
                    static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
                if (ok) {
                    success.fetch_add(1);
                } else {
                    errors.fetch_add(1);
                }
            }
        });
    }
    for (auto& t : workers) {
        t.join();
    }
    const auto done = std::chrono::steady_clock::now();

    BenchStats stats;
    stats.success = success.load();
    stats.errors = errors.load();
    stats.elapsed_ms = std::chrono::duration<double, std::milli>(done - begin).count();
    stats.qps = stats.elapsed_ms > 0.0 ? (static_cast<double>(stats.success) * 1000.0 / stats.elapsed_ms) : 0.0;
    stats.p50_us = Percentile(latencies, 0.50);
    stats.p95_us = Percentile(latencies, 0.95);
    stats.p99_us = Percentile(latencies, 0.99);
    return stats;
}

BenchStats RunWsBench(const BenchConfig& cfg,
                      const std::string& host,
                      const std::string& port,
                      bool do_warmup = true) {
    if (do_warmup && cfg.warmup > 0) {
        WarmupWs(host, port, cfg.warmup);
    }
    std::atomic<int> index{0};
    std::atomic<int> success{0};
    std::atomic<int> errors{0};
    std::vector<std::uint64_t> latencies(static_cast<std::size_t>(cfg.requests));

    auto begin = std::chrono::steady_clock::now();
    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(cfg.concurrency));
    for (int worker = 0; worker < cfg.concurrency; ++worker) {
        workers.emplace_back([&, worker] {
            try {
                net::io_context ioc;
                tcp::resolver resolver(ioc);
                websocket::stream<tcp::socket> ws(ioc);
                ws.set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
                    req.set(http::field::authorization, "Bearer user:bench-user");
                }));
                auto endpoints = resolver.resolve(host, port);
                net::connect(ws.next_layer(), endpoints);
                ws.handshake(host, "/");

                while (true) {
                    const int req_id = index.fetch_add(1);
                    if (req_id >= cfg.requests) break;
                    const auto payload = nlohmann::json{
                        {"path", "/user/create"},
                        {"body", nlohmann::json{{"nickname", "bench-ws-" + std::to_string(req_id)}}.dump()},
                        {"headers", nlohmann::json{{"x-request-id", "bench-ws-" + std::to_string(worker) + "-" + std::to_string(req_id)}}}}
                                             .dump();

                    const auto start = std::chrono::steady_clock::now();
                    ws.write(net::buffer(payload));
                    beast::flat_buffer buffer;
                    ws.read(buffer);
                    const auto end = std::chrono::steady_clock::now();

                    latencies[static_cast<std::size_t>(req_id)] =
                        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
                    const auto resp = beast::buffers_to_string(buffer.data());
                    const auto json = nlohmann::json::parse(resp, nullptr, false);
                    if (!json.is_discarded() && json.contains("user_id")) {
                        success.fetch_add(1);
                    } else {
                        errors.fetch_add(1);
                    }
                }
                ws.close(websocket::close_code::normal);
            } catch (...) {
                errors.fetch_add(1);
            }
        });
    }
    for (auto& t : workers) {
        t.join();
    }
    const auto done = std::chrono::steady_clock::now();

    BenchStats stats;
    stats.success = success.load();
    stats.errors = errors.load();
    stats.elapsed_ms = std::chrono::duration<double, std::milli>(done - begin).count();
    stats.qps = stats.elapsed_ms > 0.0 ? (static_cast<double>(stats.success) * 1000.0 / stats.elapsed_ms) : 0.0;
    stats.p50_us = Percentile(latencies, 0.50);
    stats.p95_us = Percentile(latencies, 0.95);
    stats.p99_us = Percentile(latencies, 0.99);
    return stats;
}

struct MixedBenchStats {
    BenchStats http;
    BenchStats ws;
    double elapsed_ms = 0.0;
};

MixedBenchStats RunMixedBench(const BenchConfig& cfg, const std::string& host, const std::string& port) {
    const int http_requests = std::clamp(
        static_cast<int>(std::llround(static_cast<double>(cfg.requests) * cfg.mixed_http_ratio)),
        0,
        cfg.requests);
    const int ws_requests = std::max(0, cfg.requests - http_requests);

    int http_concurrency = std::clamp(
        static_cast<int>(std::llround(static_cast<double>(cfg.concurrency) * cfg.mixed_http_ratio)),
        0,
        cfg.concurrency);
    int ws_concurrency = std::max(0, cfg.concurrency - http_concurrency);

    if (http_requests > 0 && http_concurrency == 0) {
        http_concurrency = 1;
        if (ws_concurrency > 0) --ws_concurrency;
    }
    if (ws_requests > 0 && ws_concurrency == 0) {
        ws_concurrency = 1;
        if (http_concurrency > 0) --http_concurrency;
    }

    const int http_warmup = std::clamp(
        static_cast<int>(std::llround(static_cast<double>(cfg.warmup) * cfg.mixed_http_ratio)),
        0,
        cfg.warmup);
    const int ws_warmup = std::max(0, cfg.warmup - http_warmup);
    if (http_warmup > 0) {
        WarmupHttp(host, port, http_warmup);
    }
    if (ws_warmup > 0) {
        WarmupWs(host, port, ws_warmup);
    }

    BenchConfig http_cfg = cfg;
    http_cfg.requests = http_requests;
    http_cfg.concurrency = http_concurrency;
    http_cfg.warmup = 0;

    BenchConfig ws_cfg = cfg;
    ws_cfg.requests = ws_requests;
    ws_cfg.concurrency = ws_concurrency;
    ws_cfg.warmup = 0;

    MixedBenchStats mixed_stats;
    const auto begin = std::chrono::steady_clock::now();
    std::thread http_thread([&] {
        if (http_cfg.requests > 0 && http_cfg.concurrency > 0) {
            mixed_stats.http = RunHttpBench(http_cfg, host, port, false);
        }
    });
    std::thread ws_thread([&] {
        if (ws_cfg.requests > 0 && ws_cfg.concurrency > 0) {
            mixed_stats.ws = RunWsBench(ws_cfg, host, port, false);
        }
    });
    http_thread.join();
    ws_thread.join();
    const auto done = std::chrono::steady_clock::now();
    mixed_stats.elapsed_ms = std::chrono::duration<double, std::milli>(done - begin).count();
    return mixed_stats;
}

nlohmann::json BenchStatsToJson(const BenchStats& stats) {
    return nlohmann::json{
        {"success", stats.success},
        {"errors", stats.errors},
        {"elapsed_ms", stats.elapsed_ms},
        {"qps", stats.qps},
        {"latency_us",
         nlohmann::json{
             {"p50", stats.p50_us},
             {"p95", stats.p95_us},
             {"p99", stats.p99_us}}}};
}
}  // namespace

int main(int argc, char** argv) {
    BenchConfig cfg;
    if (!ParseArgs(argc, argv, &cfg)) {
        std::cerr << "usage: gateway_http_ws_bench --mode http|ws|mixed "
                     "[--concurrency N] [--requests N] [--warmup N] [--io-threads N] "
                     "[--mixed-http-ratio 0.0-1.0] [--output FILE]\n";
        return 2;
    }
    kd39::common::log::SetLogLevel("warn");

    auto mysql = kd39::infrastructure::storage::mysql::ConnectionPool::Create({"127.0.0.1", 3306, "root", "", "kd39", 2});
    auto redis = kd39::infrastructure::storage::redis::RedisClient::Create({"127.0.0.1", 6379, "", 0, 2});
    kd39::services::user::UserServiceImpl user_service({mysql, redis});

    int grpc_port = 0;
    grpc::ServerBuilder grpc_builder;
    grpc_builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &grpc_port);
    grpc_builder.RegisterService(&user_service);
    auto grpc_server = grpc_builder.BuildAndStart();
    if (!grpc_server) {
        std::cerr << "failed to start embedded grpc server\n";
        return 1;
    }

    auto router = std::make_shared<kd39::gateways::access::GrpcRouter>(
        kd39::gateways::access::RouterTargets{"127.0.0.1:50051", "127.0.0.1:" + std::to_string(grpc_port), "127.0.0.1:50053"});
    auto auth = std::make_shared<kd39::gateways::access::AuthMiddleware>();
    const kd39::gateways::access::ServerRuntimeOptions runtime{cfg.io_threads};

    nlohmann::json result;
    if (cfg.mode == "http") {
        BenchStats stats;
        kd39::gateways::access::HttpServer http_server("127.0.0.1", 0, router, auth, runtime);
        http_server.Start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const auto port = std::to_string(http_server.bound_port());
        stats = RunHttpBench(cfg, "127.0.0.1", port);
        http_server.Stop();
        result = nlohmann::json{
            {"mode", cfg.mode},
            {"concurrency", cfg.concurrency},
            {"requests", cfg.requests},
            {"warmup", cfg.warmup},
            {"io_threads", cfg.io_threads}};
        result.update(BenchStatsToJson(stats));
    } else if (cfg.mode == "ws") {
        BenchStats stats;
        kd39::gateways::access::HttpServer http_server("127.0.0.1", 0, router, auth, runtime);
        http_server.Start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const auto port = std::to_string(http_server.bound_port());
        stats = RunWsBench(cfg, "127.0.0.1", port);
        http_server.Stop();
        result = nlohmann::json{
            {"mode", cfg.mode},
            {"concurrency", cfg.concurrency},
            {"requests", cfg.requests},
            {"warmup", cfg.warmup},
            {"io_threads", cfg.io_threads}};
        result.update(BenchStatsToJson(stats));
    } else {
        const int http_requests = std::clamp(
            static_cast<int>(std::llround(static_cast<double>(cfg.requests) * cfg.mixed_http_ratio)),
            0,
            cfg.requests);
        const int ws_requests = std::max(0, cfg.requests - http_requests);
        kd39::gateways::access::HttpServer http_server("127.0.0.1", 0, router, auth, runtime);
        http_server.Start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const auto port = std::to_string(http_server.bound_port());
        const auto mixed_stats = RunMixedBench(cfg, "127.0.0.1", port);
        http_server.Stop();
        const int total_success = mixed_stats.http.success + mixed_stats.ws.success;
        const int total_errors = mixed_stats.http.errors + mixed_stats.ws.errors;
        const double total_qps =
            mixed_stats.elapsed_ms > 0.0 ? (static_cast<double>(total_success) * 1000.0 / mixed_stats.elapsed_ms) : 0.0;
        result = nlohmann::json{
            {"mode", cfg.mode},
            {"concurrency", cfg.concurrency},
            {"requests", cfg.requests},
            {"warmup", cfg.warmup},
            {"io_threads", cfg.io_threads},
            {"mixed_http_ratio", cfg.mixed_http_ratio},
            {"split", nlohmann::json{
                          {"http_requests", http_requests},
                          {"ws_requests", ws_requests}}},
            {"success", total_success},
            {"errors", total_errors},
            {"elapsed_ms", mixed_stats.elapsed_ms},
            {"qps", total_qps},
            {"protocols", nlohmann::json{
                              {"http", BenchStatsToJson(mixed_stats.http)},
                              {"ws", BenchStatsToJson(mixed_stats.ws)}}}};
    }

    grpc_server->Shutdown();

    if (!cfg.output_file.empty()) {
        std::ofstream out(cfg.output_file);
        if (!out.is_open()) {
            std::cerr << "failed to open output file: " << cfg.output_file << "\n";
            return 1;
        }
        out << result.dump(2) << "\n";
    }
    std::cout << result.dump(2) << "\n";
    return 0;
}
