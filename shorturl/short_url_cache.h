#ifndef SHORTURL_SHORT_URL_CACHE_H
#define SHORTURL_SHORT_URL_CACHE_H

#include "bloom_filter.h"
#include "singleflight.h"
#include "../config/config.h"
#include "../CGImysql/sql_connection_pool.h"
#include <memory>
#include <mutex>
#include <random>
#include <string>

#ifdef HAVE_REDIS_PLUS_PLUS
#include <sw/redis++/redis++.h>
#endif

class ShortUrlCache {
public:
    enum class CacheStatus {
        Hit,
        Miss,
        Filtered,
        Unavailable
    };

    static ShortUrlCache& instance();

    void init(const Config& config);
    bool warmup(connection_pool* pool, std::string* error = nullptr);

    CacheStatus get(const std::string& code, std::string* long_url,
                    std::string* error = nullptr);
    bool set(const std::string& code, const std::string& long_url,
             std::string* error = nullptr);
    bool erase(const std::string& code, std::string* error = nullptr);
    void add_legal_code(const std::string& code);

    std::shared_ptr<std::mutex> rebuild_mutex(const std::string& code);
    bool enabled() const;
    bool redis_available() const;

private:
    ShortUrlCache();

    int ttl_with_jitter();
    std::string cache_key(const std::string& code) const;

    bool enabled_;
    bool redis_available_;
    bool bloom_ready_;
    int ttl_seconds_;
    int ttl_jitter_seconds_;
    int bloom_bits_;
    int bloom_hashes_;

    mutable std::mutex mutex_;
    std::mt19937 rng_;
    BloomFilter bloom_;
    SingleFlight singleflight_;

#ifdef HAVE_REDIS_PLUS_PLUS
    std::unique_ptr<sw::redis::Redis> redis_;
#endif
};

#endif
