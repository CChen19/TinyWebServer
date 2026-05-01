#ifndef OBSERVABILITY_STRUCTURED_LOGGER_H
#define OBSERVABILITY_STRUCTURED_LOGGER_H

#include "../config/config.h"
#include "../http/request.h"
#include <mutex>
#include <string>

class StructuredLogger {
public:
    static StructuredLogger& instance();

    void init(const Config& config);
    void log_access(const HttpRequest& req,
                    int status,
                    double duration_ms,
                    const std::string& remote_addr);

private:
    StructuredLogger();

    std::string json_escape(const std::string& value) const;
    std::string now_iso8601() const;

    bool enabled_;
    std::string path_;
    mutable std::mutex mutex_;
};

#endif
