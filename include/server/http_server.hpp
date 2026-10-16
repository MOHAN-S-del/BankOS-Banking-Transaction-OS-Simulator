#pragma once

#include "common/types.hpp"
#include "telemetry/metrics_sampler.hpp"
#include "storage/ledger_engine.hpp"
#include "scheduler/adaptive_scheduler.hpp"
#include "suite/benchmark_runner.hpp"

#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <memory>

namespace adaptive_os {

class HttpServer {
public:
    HttpServer(int port,
               std::shared_ptr<LedgerEngine> ledger,
               std::shared_ptr<AdaptiveScheduler> scheduler,
               std::shared_ptr<MetricsSampler> sampler,
               std::string web_dir = "web");

    ~HttpServer();

    void start();
    void stop();

    int port() const { return port_; }

private:
    void server_loop();
    void handle_client(int client_fd);

    std::string handle_get_status();
    std::string handle_get_accounts();
    std::string handle_run_benchmark();
    std::string handle_trigger_spike(const std::string& path);
    std::string serve_static_file(const std::string& path);

    int port_;
    std::string web_dir_;
    std::shared_ptr<LedgerEngine> ledger_;
    std::shared_ptr<AdaptiveScheduler> scheduler_;
    std::shared_ptr<MetricsSampler> sampler_;

    std::atomic<bool> running_;
    int server_fd_;
    std::thread server_thread_;
};

} // namespace adaptive_os
