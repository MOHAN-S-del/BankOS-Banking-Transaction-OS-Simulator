#include "server/http_server.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <sstream>
#include <fstream>
#include <iostream>
#include <cstring>

namespace adaptive_os {

HttpServer::HttpServer(int port,
                       std::shared_ptr<LedgerEngine> ledger,
                       std::shared_ptr<AdaptiveScheduler> scheduler,
                       std::shared_ptr<MetricsSampler> sampler,
                       std::string web_dir)
    : port_(port),
      web_dir_(std::move(web_dir)),
      ledger_(std::move(ledger)),
      scheduler_(std::move(scheduler)),
      sampler_(std::move(sampler)),
      running_(false),
      server_fd_(-1) {}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::start() {
    if (running_.exchange(true)) return;

    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        std::cerr << "[HttpServer] Failed to create socket\n";
        running_ = false;
        return;
    }

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[HttpServer] Bind failed on port " << port_ << "\n";
        close(server_fd_);
        server_fd_ = -1;
        running_ = false;
        return;
    }

    if (listen(server_fd_, 64) < 0) {
        std::cerr << "[HttpServer] Listen failed\n";
        close(server_fd_);
        server_fd_ = -1;
        running_ = false;
        return;
    }

    std::cout << "[HttpServer] Adaptive Ledger OS Web & REST API listening at http://localhost:" << port_ << "\n";
    server_thread_ = std::thread(&HttpServer::server_loop, this);
}

void HttpServer::stop() {
    if (!running_.exchange(false)) return;

    if (server_fd_ >= 0) {
        shutdown(server_fd_, SHUT_RDWR);
        close(server_fd_);
        server_fd_ = -1;
    }

    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

void HttpServer::server_loop() {
    while (running_.load()) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);

        if (client_fd < 0) {
            if (!running_.load()) break;
            continue;
        }

        std::thread([this, client_fd]() {
            handle_client(client_fd);
            close(client_fd);
        }).detach();
    }
}

void HttpServer::handle_client(int client_fd) {
    char buffer[4096];
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) return;

    buffer[bytes_read] = '\0';
    std::string request(buffer);

    std::istringstream req_stream(request);
    std::string method, path, proto;
    req_stream >> method >> path >> proto;

    std::string body;
    std::string content_type = "application/json";
    int status_code = 200;

    if (path == "/api/status") {
        body = handle_get_status();
    } else if (path == "/api/accounts") {
        body = handle_get_accounts();
    } else if (path == "/api/benchmark" || path == "/api/workload/burst") {
        body = handle_run_benchmark();
    } else {
        // Serve static web UI
        if (path == "/" || path.empty()) {
            path = "/index.html";
        }
        body = serve_static_file(path);
        if (path.find(".html") != std::string::npos) content_type = "text/html";
        else if (path.find(".css") != std::string::npos) content_type = "text/css";
        else if (path.find(".js") != std::string::npos) content_type = "application/javascript";
        else content_type = "text/plain";

        if (body.empty()) {
            status_code = 404;
            body = "{\"error\": \"Not Found\"}";
            content_type = "application/json";
        }
    }

    std::ostringstream response;
    response << "HTTP/1.1 " << status_code << " OK\r\n";
    response << "Content-Type: " << content_type << "\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "Access-Control-Allow-Origin: *\r\n";
    response << "Connection: close\r\n\r\n";
    response << body;

    std::string res_str = response.str();
    write(client_fd, res_str.c_str(), res_str.size());
}

std::string HttpServer::handle_get_status() {
    auto snap = sampler_->latest_snapshot();
    std::ostringstream oss;
    oss << "{"
        << "\"composite_index\":" << snap.composite_index << ","
        << "\"conflict_rate\":" << snap.conflict_rate << ","
        << "\"abort_rate\":" << snap.abort_rate << ","
        << "\"lock_queue_depth\":" << snap.lock_queue_depth << ","
        << "\"retry_intensity\":" << snap.retry_intensity << ","
        << "\"hysteresis_state\":\"" << hysteresisStateToString(snap.state) << "\","
        << "\"effective_admission_rate\":" << snap.effective_admission_rate << ","
        << "\"total_submitted\":" << scheduler_->total_submitted() << ","
        << "\"total_committed\":" << scheduler_->total_committed() << ","
        << "\"total_aborted\":" << scheduler_->total_aborted() << ","
        << "\"total_retries\":" << scheduler_->total_retries() << ","
        << "\"total_throttled\":" << scheduler_->total_throttled() << ","
        << "\"total_hotspot_demotions\":" << scheduler_->total_hotspot_demotions() << ","
        << "\"starvation_migrations\":" << snap.starvation_migrations << ","
        << "\"active_workers\":" << scheduler_->active_workers() << ","
        << "\"num_workers\":" << scheduler_->num_workers() << ","
        << "\"q1_depth\":" << snap.q1_depth << ","
        << "\"q2_depth\":" << snap.q2_depth << ","
        << "\"q3_depth\":" << snap.q3_depth << ","
        << "\"bounded_wait_tw_ms\":" << sampler_->bounded_wait_timeout().count() << ","
        << "\"avg_latency_ms\":" << snap.avg_latency_ms
        << "}";
    return oss.str();
}

std::string HttpServer::handle_get_accounts() {
    auto accounts = ledger_->get_all_accounts();
    std::ostringstream oss;
    oss << "[";
    bool first = true;
    for (const auto& acc : accounts) {
        if (!first) oss << ",";
        first = false;
        oss << "{"
            << "\"id\":" << acc->id() << ","
            << "\"type\":\"" << accountTypeToString(acc->type()) << "\","
            << "\"owner\":\"" << acc->owner() << "\","
            << "\"balance\":" << acc->balance() << ","
            << "\"waiters\":" << acc->lock_waiters() << ","
            << "\"contention_hits\":" << acc->contention_hits()
            << "}";
    }
    oss << "]";
    return oss.str();
}

std::string HttpServer::handle_run_benchmark() {
    auto [b, a] = BenchmarkRunner::run_comparative_benchmark(2.0, 8, 40, 10, 50);

    std::ostringstream oss;
    oss << "{"
        << "\"baseline\":{"
        << "\"throughput_tps\":" << b.throughput_tps << ","
        << "\"abort_rate_percent\":" << b.abort_rate_percent << ","
        << "\"p50_latency_ms\":" << b.p50_latency_ms << ","
        << "\"p95_latency_ms\":" << b.p95_latency_ms << ","
        << "\"p99_latency_ms\":" << b.p99_latency_ms << ","
        << "\"retail_p95_ms\":" << b.retail_p95_latency_ms << ","
        << "\"settlement_p95_ms\":" << b.settlement_p95_latency_ms << ","
        << "\"context_switches\":" << b.context_switches_estimated
        << "},"
        << "\"adaptive\":{"
        << "\"throughput_tps\":" << a.throughput_tps << ","
        << "\"abort_rate_percent\":" << a.abort_rate_percent << ","
        << "\"p50_latency_ms\":" << a.p50_latency_ms << ","
        << "\"p95_latency_ms\":" << a.p95_latency_ms << ","
        << "\"p99_latency_ms\":" << a.p99_latency_ms << ","
        << "\"retail_p95_ms\":" << a.retail_p95_latency_ms << ","
        << "\"settlement_p95_ms\":" << a.settlement_p95_latency_ms << ","
        << "\"starvation_migrations\":" << a.starvation_migrations << ","
        << "\"hotspot_demotions\":" << a.hotspot_demotions << ","
        << "\"context_switches\":" << a.context_switches_estimated
        << "}"
        << "}";
    return oss.str();
}

std::string HttpServer::serve_static_file(const std::string& path) {
    std::string safe_path = web_dir_ + path;
    std::ifstream file(safe_path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return "";
    }
    std::ostringstream oss;
    oss << file.rdbuf();
    return oss.str();
}

} // namespace adaptive_os
