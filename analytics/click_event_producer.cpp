#include "click_event_producer.h"
#include "../shorturl/base62.h"
#include "../shorturl/snowflake.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdio>

ClickEventProducer& ClickEventProducer::instance() {
    static ClickEventProducer producer;
    return producer;
}

ClickEventProducer::ClickEventProducer()
    : enabled_(false), available_(false)
#ifdef HAVE_RDKAFKA
      , producer_(nullptr)
#endif
{}

ClickEventProducer::~ClickEventProducer() {
#ifdef HAVE_RDKAFKA
    if (producer_) {
        rd_kafka_flush(producer_, 3000);
        rd_kafka_destroy(producer_);
    }
#endif
}

void ClickEventProducer::init(const Config& config) {
    std::lock_guard<std::mutex> guard(mutex_);
    enabled_ = config.kafka_enabled;
    available_ = false;
    topic_ = config.kafka_click_topic;

    if (!enabled_) {
        return;
    }

#ifdef HAVE_RDKAFKA
    char errstr[512];
    rd_kafka_conf_t* conf = rd_kafka_conf_new();

    auto set_conf = [&](const char* key, const std::string& value) -> bool {
        rd_kafka_conf_res_t res =
            rd_kafka_conf_set(conf, key, value.c_str(), errstr, sizeof(errstr));
        return res == RD_KAFKA_CONF_OK;
    };

    bool ok = true;
    ok = ok && set_conf("bootstrap.servers", config.kafka_brokers);
    ok = ok && set_conf("acks", "all");
    ok = ok && set_conf("enable.idempotence", "true");
    ok = ok && set_conf("retries", std::to_string(config.kafka_retries));
    ok = ok && set_conf("message.timeout.ms", std::to_string(config.kafka_message_timeout_ms));
    ok = ok && set_conf("linger.ms", std::to_string(config.kafka_linger_ms));

    if (!ok) {
        fprintf(stderr, "Kafka producer config error: %s\n", errstr);
        rd_kafka_conf_destroy(conf);
        return;
    }

    producer_ = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
    if (!producer_) {
        fprintf(stderr, "Kafka producer init error: %s\n", errstr);
        return;
    }

    available_ = true;
#endif
}

bool ClickEventProducer::publish_click(const HttpRequest& req,
                                       const std::string& code,
                                       std::string* error) {
    if (!enabled_) {
        return false;
    }

#ifdef HAVE_RDKAFKA
    std::lock_guard<std::mutex> guard(mutex_);
    if (!available_ || !producer_) {
        if (error) *error = "kafka producer unavailable";
        fprintf(stderr, "Kafka producer unavailable for click code=%s\n", code.c_str());
        return false;
    }

    const std::string payload = build_event_json(req, code);
    rd_kafka_resp_err_t err = rd_kafka_producev(
        producer_,
        RD_KAFKA_V_TOPIC(topic_.c_str()),
        RD_KAFKA_V_KEY(const_cast<char*>(code.data()), code.size()),
        RD_KAFKA_V_VALUE(const_cast<char*>(payload.data()), payload.size()),
        RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY),
        RD_KAFKA_V_END);

    rd_kafka_poll(producer_, 0);

    if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
        if (error) *error = rd_kafka_err2str(err);
        fprintf(stderr, "Kafka produce error for click code=%s: %s\n",
                code.c_str(), rd_kafka_err2str(err));
        return false;
    }
    rd_kafka_poll(producer_, 10);
    return true;
#else
    (void)req;
    (void)code;
    if (error) *error = "librdkafka not linked";
    return false;
#endif
}

bool ClickEventProducer::enabled() const {
    return enabled_;
}

bool ClickEventProducer::available() const {
    return available_;
}

std::string ClickEventProducer::build_event_json(const HttpRequest& req,
                                                 const std::string& code) {
    nlohmann::json event = {
        {"event_id", next_event_id()},
        {"short_code", code},
        {"clicked_at_ms", now_ms()},
        {"user_agent", header_or_empty(req, "User-Agent")},
        {"referer", header_or_empty(req, "Referer")},
        {"x_forwarded_for", header_or_empty(req, "X-Forwarded-For")}
    };
    return event.dump();
}

std::string ClickEventProducer::header_or_empty(const HttpRequest& req,
                                                const std::string& name) const {
    auto it = req.headers.find(name);
    return it == req.headers.end() ? "" : it->second;
}

std::string ClickEventProducer::next_event_id() {
    static SnowflakeIdGenerator generator;
    return base62_encode(generator.next_id());
}

long long ClickEventProducer::now_ms() const {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
}
