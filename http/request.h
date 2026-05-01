#ifndef REQUEST_H
#define REQUEST_H

#include <string>
#include <unordered_map>
#include <mysql/mysql.h>

struct HttpRequest {
    std::string method;        // "GET" / "POST"
    std::string path;          // "/api/shorten"
    std::string query;         // "?foo=bar"
    std::unordered_map<std::string, std::string> headers;
    std::string body;          // 原始 body，JSON 留给 Handler 自己解
    
    // 路径参数（由 Router 填充，比如 /{code} 匹配后 params["code"] = "abc123"）
    std::unordered_map<std::string, std::string> params;

    // 当前请求绑定的 MySQL 连接，由 http_conn 在线程池中注入。
    MYSQL* mysql = nullptr;
};

#endif
