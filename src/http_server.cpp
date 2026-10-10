#include "http_server.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>

namespace BankOS {

HttpServer::HttpServer(int port)
    : m_port(port), m_server_fd(-1), m_static_dir("web"), m_running(false) {}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::get(const std::string& path, HttpHandler handler) {
    m_get_routes[path] = handler;
}

void HttpServer::post(const std::string& path, HttpHandler handler) {
    m_post_routes[path] = handler;
}

void HttpServer::setStaticDir(const std::string& directory) {
    m_static_dir = directory;
}

bool HttpServer::start() {
    if (m_running) return true;

    m_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_server_fd < 0) {
        std::cerr << "[HttpServer] Failed to create socket\n";
        return false;
    }

    int opt = 1;
    setsockopt(m_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(m_port);

    if (bind(m_server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "[HttpServer] Bind failed on port " << m_port << "\n";
        close(m_server_fd);
        m_server_fd = -1;
        return false;
    }

    if (listen(m_server_fd, 64) < 0) {
        std::cerr << "[HttpServer] Listen failed\n";
        close(m_server_fd);
        m_server_fd = -1;
        return false;
    }

    m_running = true;
    m_listener_thread = std::make_unique<std::thread>(&HttpServer::acceptLoop, this);
    std::cout << "[HttpServer] Bank OS Server running at http://localhost:" << m_port << "\n";
    return true;
}

void HttpServer::stop() {
    if (!m_running) return;
    m_running = false;
    if (m_server_fd >= 0) {
        close(m_server_fd);
        m_server_fd = -1;
    }
    if (m_listener_thread && m_listener_thread->joinable()) {
        m_listener_thread->join();
    }
    m_listener_thread.reset();
}

void HttpServer::acceptLoop() {
    while (m_running) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(m_server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (!m_running) break;
            continue;
        }

        std::thread([this, client_fd]() {
            handleClient(client_fd);
        }).detach();
    }
}

std::string HttpServer::getMimeType(const std::string& path) {
    if (path.rfind(".html") != std::string::npos) return "text/html; charset=utf-8";
    if (path.rfind(".css") != std::string::npos) return "text/css";
    if (path.rfind(".js") != std::string::npos) return "application/javascript";
    if (path.rfind(".json") != std::string::npos) return "application/json";
    if (path.rfind(".svg") != std::string::npos) return "image/svg+xml";
    if (path.rfind(".png") != std::string::npos) return "image/png";
    return "text/plain";
}

void HttpServer::handleClient(int client_fd) {
    char buffer[8192];
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        close(client_fd);
        return;
    }
    buffer[bytes_read] = '\0';

    std::string request_str(buffer, bytes_read);
    std::istringstream stream(request_str);
    std::string method, full_path, version;
    stream >> method >> full_path >> version;

    // Parse path and query parameters
    std::string path = full_path;
    std::map<std::string, std::string> query_params;
    size_t q_pos = full_path.find('?');
    if (q_pos != std::string::npos) {
        path = full_path.substr(0, q_pos);
        std::string query = full_path.substr(q_pos + 1);
        std::istringstream qs(query);
        std::string pair;
        while (std::getline(qs, pair, '&')) {
            size_t eq = pair.find('=');
            if (eq != std::string::npos) {
                query_params[pair.substr(0, eq)] = pair.substr(eq + 1);
            }
        }
    }

    // Extract body
    std::string body = "";
    size_t body_pos = request_str.find("\r\n\r\n");
    if (body_pos != std::string::npos) {
        body = request_str.substr(body_pos + 4);
    }

    HttpRequest req;
    req.method = method;
    req.path = path;
    req.body = body;
    req.params = query_params;

    HttpResponse res;

    if (method == "GET") {
        auto it = m_get_routes.find(path);
        if (it != m_get_routes.end()) {
            res = it->second(req);
        } else {
            // Serve static file
            std::string filepath = m_static_dir + ((path == "/" || path.empty()) ? "/index.html" : path);
            std::ifstream file(filepath, std::ios::binary);
            if (file.is_open()) {
                std::ostringstream ss;
                ss << file.rdbuf();
                res.status_code = 200;
                res.status_text = "OK";
                res.content_type = getMimeType(filepath);
                res.body = ss.str();
            } else {
                res = HttpResponse::notFound("Static file not found: " + path);
            }
        }
    } else if (method == "POST") {
        auto it = m_post_routes.find(path);
        if (it != m_post_routes.end()) {
            res = it->second(req);
        } else {
            res = HttpResponse::notFound("API route not found: " + path);
        }
    } else if (method == "OPTIONS") {
        // Pre-flight CORS request
        res.status_code = 200;
        res.status_text = "OK";
    }

    // Build raw HTTP response
    std::ostringstream response_stream;
    response_stream << "HTTP/1.1 " << res.status_code << " " << res.status_text << "\r\n";
    response_stream << "Content-Type: " << res.content_type << "\r\n";
    response_stream << "Content-Length: " << res.body.size() << "\r\n";
    response_stream << "Access-Control-Allow-Origin: *\r\n";
    response_stream << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    response_stream << "Access-Control-Allow-Headers: Content-Type\r\n";
    response_stream << "Connection: close\r\n\r\n";
    response_stream << res.body;

    std::string response_data = response_stream.str();
    write(client_fd, response_data.c_str(), response_data.size());
    close(client_fd);
}

} // namespace BankOS
