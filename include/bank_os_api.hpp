#pragma once

#include "http_server.hpp"

namespace BankOS {

class BankOSApi {
public:
    static void registerRoutes(HttpServer& server);
    static std::string buildStateJson();
    static std::string buildComparisonDemoJson();
};

} // namespace BankOS
