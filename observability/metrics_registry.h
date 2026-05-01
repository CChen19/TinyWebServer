#ifndef OBSERVABILITY_METRICS_REGISTRY_H
#define OBSERVABILITY_METRICS_REGISTRY_H

#include <array>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

class MetricsRegistry {
public:
    static MetricsRegistry& instance();

    void observe_http_request(const std::string& method,
                              const std::string& route,
                              int status,
                              double duration_seconds);
    void observe_cache_result(const std::string& result);
    void observe_kafka_publish(bool success);

    std::string render_prometheus();

private:
    MetricsRegistry();

    std::string labels(const std::map<std::string, std::string>& values) const;
    std::string escape_label_value(const std::string& value) const;

    mutable std::mutex mutex_;
    std::map<std::string, uint64_t> http_requests_;
    std::array<uint64_t, 13> http_latency_buckets_;
    uint64_t http_latency_count_;
    double http_latency_sum_;
    std::map<std::string, uint64_t> cache_results_;
    uint64_t kafka_publish_success_;
    uint64_t kafka_publish_failure_;
};

#endif
