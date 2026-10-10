#pragma once

#include <string>
#include <functional>
#include <map>
#include <thread>
#include <atomic>
#include <memory>

namespace BankOS {

struct HttpRequest {
    std::string method;   // "GET", "POST", etc.
    std::string path;     // "/api/state", "/index.html", etc.
    std::string body;     // Raw request body
    std::map<std::string, std::string> params; // Query params
};

struct HttpResponse {
    int status_code = 200;
    std::string status_text = "OK";
    std::string content_type = "text/plain";
    std::string body = "";
    std::map<std::string, std::string> headers;

    static HttpResponse json(const std::string& json_body, int code = 200) {
        HttpResponse res;
        res.status_code = code;
        res.content_type = "application/json";
        res.body = json_body;
        return res;
    }

    static HttpResponse html(const std::string& html_body, int code = 200) {
        HttpResponse res;
        res.status_code = code;
        res.content_type = "text/html; charset=utf-8";
        res.body = html_body;
        return res;
    }

    static HttpResponse notFound(const std::string& msg = "Not Found") {
        HttpResponse res;
        res.status_code = 404;
        res.status_text = "Not Found";
        res.body = msg;
        return res;
    }
};

using HttpHandler = std::function<HttpResponse(const HttpRequest&)>;

class HttpServer {
public:
    HttpServer(int port = 8080);
    ~HttpServer();

    void get(const std::string& path, HttpHandler handler);
    void post(const std::string& path, HttpHandler handler);
    void setStaticDir(const std::string& directory);

    bool start();
    void stop();

private:
    int m_port;
    int m_server_fd;
    std::string m_static_dir;
    std::atomic<bool> m_running;
    std::unique_ptr<std::thread> m_listener_thread;

    std::map<std::string, HttpHandler> m_get_routes;
    std::map<std::string, HttpHandler> m_post_routes;

    void acceptLoop();
    void handleClient(int client_fd);
    std::string getMimeType(const std::string& filepath);
};

} // namespace BankOS
