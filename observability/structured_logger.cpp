#include "structured_logger.h"
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>

StructuredLogger& StructuredLogger::instance() {
    static StructuredLogger logger;
    return logger;
}

StructuredLogger::StructuredLogger()
    : enabled_(true), path_("./logs/access.jsonl") {}

void StructuredLogger::init(const Config& config) {
    std::lock_guard<std::mutex> guard(mutex_);
    enabled_ = config.structured_log_enabled;
    path_ = config.structured_log_path;
    if (!path_.empty() && path_.compare(0, 7, "./logs/") == 0) {
        mkdir("./logs", 0755);
    }
}

void StructuredLogger::log_access(const HttpRequest& req,
                                  int status,
                                  double duration_ms,
                                  const std::string& remote_addr) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!enabled_ || path_.empty()) {
        return;
    }
    if (path_.compare(0, 7, "./logs/") == 0) {
        mkdir("./logs", 0755);
    }

    std::ofstream out(path_.c_str(), std::ios::app);
    if (!out) {
        return;
    }

    out << "{"
        << "\"ts\":\"" << now_iso8601() << "\","
        << "\"event\":\"http_access\","
        << "\"method\":\"" << json_escape(req.method) << "\","
        << "\"path\":\"" << json_escape(req.path) << "\","
        << "\"route\":\"" << json_escape(req.route_pattern) << "\","
        << "\"status\":" << status << ","
        << "\"duration_ms\":" << std::fixed << std::setprecision(3) << duration_ms << ","
        << "\"remote_addr\":\"" << json_escape(remote_addr) << "\","
        << "\"user_agent\":\"" << json_escape(req.header_or_empty("User-Agent")) << "\""
        << "}\n";
}

std::string StructuredLogger::json_escape(const std::string& value) const {
    std::string escaped;
    escaped.reserve(value.size());
    for (char c : value) {
        switch (c) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped.push_back(c); break;
        }
    }
    return escaped;
}

std::string StructuredLogger::now_iso8601() const {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto seconds = system_clock::to_time_t(now);
    const auto millis = duration_cast<milliseconds>(
        now.time_since_epoch()).count() % 1000;

    struct tm tm_value;
    localtime_r(&seconds, &tm_value);

    std::ostringstream out;
    out << std::put_time(&tm_value, "%Y-%m-%dT%H:%M:%S")
        << "." << std::setw(3) << std::setfill('0') << millis;
    return out.str();
}
