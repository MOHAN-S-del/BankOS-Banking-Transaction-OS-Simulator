#pragma once

#include <string>
#include <map>
#include <functional>
#include <thread>
#include <atomic>
#include <iostream>
#include <sstream>
#include <fstream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>

namespace httplib {

struct Request {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
};

struct Response {
    int status = 200;
    std::string body;
    std::string content_type = "text/plain";
    std::map<std::string, std::string> headers;

    void set_content(const std::string& s, const char* type) {
        body = s;
        content_type = type;
    }

    void set_header(const std::string& key, const std::string& val) {
        headers[key] = val;
    }
};

class Server {
public:
    using Handler = std::function<void(const Request&, Response&)>;

    Server() : server_fd_(-1), running_(false) {}

    ~Server() {
        stop();
    }

    Server& Get(const std::string& pattern, Handler handler) {
        get_handlers_[pattern] = std::move(handler);
        return *this;
    }

    bool set_mount_point(const std::string& mount_point, const std::string& dir) {
        mount_point_ = mount_point;
        base_dir_ = dir;
        return true;
    }

    bool listen(const char* host, int port) {
        (void)host;
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ < 0) return false;

        int opt = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(server_fd_);
            server_fd_ = -1;
            return false;
        }

        if (::listen(server_fd_, 64) < 0) {
            close(server_fd_);
            server_fd_ = -1;
            return false;
        }

        running_ = true;
        std::cout << "[httplib::Server] Listening at http://localhost:" << port << "\n";

        while (running_) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);

            if (client_fd < 0) {
                if (!running_) break;
                continue;
            }

            std::thread([this, client_fd]() {
                handle_client(client_fd);
                close(client_fd);
            }).detach();
        }

        return true;
    }

    void stop() {
        if (!running_.exchange(false)) return;
        if (server_fd_ >= 0) {
            shutdown(server_fd_, SHUT_RDWR);
            close(server_fd_);
            server_fd_ = -1;
        }
    }

    bool is_running() const { return running_; }

private:
    void handle_client(int client_fd) {
        char buffer[4096];
        ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) return;

        buffer[bytes_read] = '\0';
        std::istringstream req_stream(buffer);
        std::string method, path, proto;
        req_stream >> method >> path >> proto;

        Request req;
        req.method = method;
        req.path = path;

        Response res;

        // Check registered handlers
        std::string path_without_query = path;
        auto query_pos = path.find('?');
        if (query_pos != std::string::npos) {
            path_without_query = path.substr(0, query_pos);
        }

        auto it = get_handlers_.find(path_without_query);
        if (it != get_handlers_.end()) {
            it->second(req, res);
        } else if (!base_dir_.empty()) {
            // Serve static file
            serve_file(path_without_query, res);
        } else {
            res.status = 404;
            res.set_content("{\"error\":\"Not Found\"}", "application/json");
        }

        std::ostringstream response;
        response << "HTTP/1.1 " << res.status << " OK\r\n";
        response << "Content-Type: " << res.content_type << "\r\n";
        response << "Content-Length: " << res.body.size() << "\r\n";
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "Connection: close\r\n";

        for (const auto& [k, v] : res.headers) {
            response << k << ": " << v << "\r\n";
        }
        response << "\r\n";
        response << res.body;

        std::string res_str = response.str();
        write(client_fd, res_str.c_str(), res_str.size());
    }

    void serve_file(const std::string& path, Response& res) {
        std::string file_path = path;
        if (file_path == "/" || file_path.empty()) {
            file_path = "/index.html";
        }

        std::string full_path = base_dir_ + file_path;
        std::ifstream file(full_path, std::ios::in | std::ios::binary);
        if (!file.is_open()) {
            res.status = 404;
            res.set_content("File Not Found", "text/plain");
            return;
        }

        std::ostringstream oss;
        oss << file.rdbuf();
        res.body = oss.str();
        res.status = 200;

        if (full_path.find(".html") != std::string::npos) res.content_type = "text/html";
        else if (full_path.find(".css") != std::string::npos) res.content_type = "text/css";
        else if (full_path.find(".js") != std::string::npos) res.content_type = "application/javascript";
        else if (full_path.find(".json") != std::string::npos) res.content_type = "application/json";
        else res.content_type = "text/plain";
    }

    int server_fd_;
    std::atomic<bool> running_;
    std::string mount_point_;
    std::string base_dir_;
    std::map<std::string, Handler> get_handlers_;
};

} // namespace httplib
