#ifndef ANALYTICS_CLICK_EVENT_PRODUCER_H
#define ANALYTICS_CLICK_EVENT_PRODUCER_H

#include "../config/config.h"
#include "../http/request.h"
#include <memory>
#include <mutex>
#include <string>

#ifdef HAVE_RDKAFKA
#include <librdkafka/rdkafka.h>
#endif

class ClickEventProducer {
public:
    static ClickEventProducer& instance();

    void init(const Config& config);
    bool publish_click(const HttpRequest& req, const std::string& code,
                       std::string* error = nullptr);
    bool enabled() const;
    bool available() const;

private:
    ClickEventProducer();
    ~ClickEventProducer();

    std::string build_event_json(const HttpRequest& req, const std::string& code);
    std::string header_or_empty(const HttpRequest& req, const std::string& name) const;
    std::string next_event_id();
    long long now_ms() const;

    bool enabled_;
    bool available_;
    std::string topic_;
    mutable std::mutex mutex_;

#ifdef HAVE_RDKAFKA
    rd_kafka_t* producer_;
#endif
};

#endif
