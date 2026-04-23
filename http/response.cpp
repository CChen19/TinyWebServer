#include "response.h"

void HttpResponse::set_status(int code) { status_code_ = code; }

void HttpResponse::set_header(const std::string& k, const std::string& v) {
    headers_[k] = v;
}

void HttpResponse::set_body(const std::string& body) { body_ = body; }

void HttpResponse::set_json(const nlohmann::json& j) {
    headers_["Content-Type"] = "application/json";
    body_ = j.dump();
}

std::string HttpResponse::serialize() const {
    std::string out;
    out.reserve(256 + body_.size());

    out += "HTTP/1.1 ";
    out += std::to_string(status_code_);
    out += ' ';
    out += status_text(status_code_);
    out += "\r\n";

    out += "Content-Length: ";
    out += std::to_string(body_.size());
    out += "\r\n";

    for (const auto& h : headers_) {
        out += h.first;
        out += ": ";
        out += h.second;
        out += "\r\n";
    }

    out += "\r\n";
    out += body_;

    return out;
}

const char* HttpResponse::status_text(int code) {
    switch (code) {
        case 200: return "OK";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        default:  return "Unknown";
    }
}
