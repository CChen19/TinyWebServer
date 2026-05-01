#include "metrics_registry.h"
#include <iomanip>
#include <sstream>

namespace {

const double kLatencyBuckets[] = {
    0.001, 0.005, 0.010, 0.025, 0.050, 0.100, 0.250,
    0.500, 1.000, 2.500, 5.000, 10.000
};

std::string status_class(int status) {
    if (status >= 100 && status < 600) {
        return std::to_string(status / 100) + "xx";
    }
    return "unknown";
}

} // namespace

MetricsRegistry& MetricsRegistry::instance() {
    static MetricsRegistry registry;
    return registry;
}

MetricsRegistry::MetricsRegistry()
    : http_latency_buckets_(), http_latency_count_(0), http_latency_sum_(0.0),
      kafka_publish_success_(0), kafka_publish_failure_(0) {}

void MetricsRegistry::observe_http_request(const std::string& method,
                                           const std::string& route,
                                           int status,
                                           double duration_seconds) {
    std::lock_guard<std::mutex> guard(mutex_);
    const std::string key = method + "|" + route + "|" + status_class(status);
    http_requests_[key]++;
    for (size_t i = 0; i < sizeof(kLatencyBuckets) / sizeof(kLatencyBuckets[0]); ++i) {
        if (duration_seconds <= kLatencyBuckets[i]) {
            http_latency_buckets_[i]++;
        }
    }
    http_latency_buckets_[http_latency_buckets_.size() - 1]++;
    http_latency_count_++;
    http_latency_sum_ += duration_seconds;
}

void MetricsRegistry::observe_cache_result(const std::string& result) {
    std::lock_guard<std::mutex> guard(mutex_);
    cache_results_[result]++;
}

void MetricsRegistry::observe_kafka_publish(bool success) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (success) {
        kafka_publish_success_++;
    } else {
        kafka_publish_failure_++;
    }
}

std::string MetricsRegistry::render_prometheus() {
    std::lock_guard<std::mutex> guard(mutex_);
    std::ostringstream out;
    out << std::fixed << std::setprecision(6);

    out << "# HELP shorturl_http_requests_total Total HTTP requests by route and status class.\n";
    out << "# TYPE shorturl_http_requests_total counter\n";
    for (const auto& item : http_requests_) {
        size_t first = item.first.find('|');
        size_t second = item.first.find('|', first + 1);
        std::map<std::string, std::string> label_values = {
            {"method", item.first.substr(0, first)},
            {"route", item.first.substr(first + 1, second - first - 1)},
            {"status_class", item.first.substr(second + 1)}
        };
        out << "shorturl_http_requests_total" << labels(label_values)
            << " " << item.second << "\n";
    }

    out << "# HELP shorturl_http_request_duration_seconds HTTP request latency histogram.\n";
    out << "# TYPE shorturl_http_request_duration_seconds histogram\n";
    for (size_t i = 0; i < sizeof(kLatencyBuckets) / sizeof(kLatencyBuckets[0]); ++i) {
        out << "shorturl_http_request_duration_seconds_bucket"
            << labels({{"le", std::to_string(kLatencyBuckets[i])}})
            << " " << http_latency_buckets_[i] << "\n";
    }
    out << "shorturl_http_request_duration_seconds_bucket"
        << labels({{"le", "+Inf"}}) << " "
        << http_latency_buckets_[http_latency_buckets_.size() - 1] << "\n";
    out << "shorturl_http_request_duration_seconds_sum " << http_latency_sum_ << "\n";
    out << "shorturl_http_request_duration_seconds_count " << http_latency_count_ << "\n";

    out << "# HELP shorturl_cache_requests_total Cache lookups by result.\n";
    out << "# TYPE shorturl_cache_requests_total counter\n";
    for (const auto& item : cache_results_) {
        out << "shorturl_cache_requests_total"
            << labels({{"result", item.first}})
            << " " << item.second << "\n";
    }

    out << "# HELP shorturl_kafka_publish_total Kafka click event publish attempts.\n";
    out << "# TYPE shorturl_kafka_publish_total counter\n";
    out << "shorturl_kafka_publish_total" << labels({{"result", "success"}})
        << " " << kafka_publish_success_ << "\n";
    out << "shorturl_kafka_publish_total" << labels({{"result", "failure"}})
        << " " << kafka_publish_failure_ << "\n";

    return out.str();
}

std::string MetricsRegistry::labels(
    const std::map<std::string, std::string>& values) const {
    if (values.empty()) {
        return "";
    }
    std::ostringstream out;
    out << "{";
    bool first = true;
    for (const auto& item : values) {
        if (!first) {
            out << ",";
        }
        first = false;
        out << item.first << "=\"" << escape_label_value(item.second) << "\"";
    }
    out << "}";
    return out.str();
}

std::string MetricsRegistry::escape_label_value(const std::string& value) const {
    std::string escaped;
    escaped.reserve(value.size());
    for (char c : value) {
        if (c == '\\' || c == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(c);
    }
    return escaped;
}
