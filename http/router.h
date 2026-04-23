#ifndef ROUTER_H
#define ROUTER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include "request.h"
#include "response.h"

using HandlerFunc = std::function<void(const HttpRequest&, HttpResponse&)>;

class Router {
public:
    static Router& instance();
    
    void get(const std::string& pattern, HandlerFunc h);
    void post(const std::string& pattern, HandlerFunc h);
    
    // 匹配 URL，填充 req.params，调用对应 handler
    // 返回 false 表示没匹配上（404）
    bool dispatch(HttpRequest& req, HttpResponse& resp);
    
private:
    struct Route {
        std::string method;
        std::vector<std::string> segments;   // ["api", "stats", "{code}"]
        HandlerFunc handler;
    };
    std::vector<Route> routes_;
    
    bool match(const Route& r, const HttpRequest& req,
               std::unordered_map<std::string, std::string>& params);
};

#endif
