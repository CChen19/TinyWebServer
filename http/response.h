#ifndef RESPONSE_H
#define RESPONSE_H

#include <string>
#include <unordered_map>
#include "../third_party/nlohmann/json.hpp"

class HttpResponse {
public:
    void set_status(int code);
    void set_header(const std::string& k, const std::string& v);
    void set_body(const std::string& body);
    void set_json(const nlohmann::json& j);

    int status_code() const { return status_code_; }
    std::string serialize() const;

private:
    int status_code_ = 200;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;

    static const char* status_text(int code);
};

#endif
