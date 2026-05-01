#include "short_url_cache.h"
#include "short_url_repository.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <vector>

namespace {

#ifdef HAVE_REDIS_PLUS_PLUS
void fill_redis_options(const std::string& uri,
                        sw::redis::ConnectionOptions* options) {
    std::string endpoint = uri;
    const std::string tcp_prefix = "tcp://";
    const std::string redis_prefix = "redis://";

    if (endpoint.compare(0, tcp_prefix.size(), tcp_prefix) == 0) {
        endpoint = endpoint.substr(tcp_prefix.size());
    } else if (endpoint.compare(0, redis_prefix.size(), redis_prefix) == 0) {
        endpoint = endpoint.substr(redis_prefix.size());
    }

    const std::string::size_type slash = endpoint.find('/');
    if (slash != std::string::npos) {
        endpoint = endpoint.substr(0, slash);
    }

    const std::string::size_type colon = endpoint.rfind(':');
    if (colon == std::string::npos) {
        options->host = endpoint.empty() ? "127.0.0.1" : endpoint;
        return;
    }

    options->host = colon == 0 ? "127.0.0.1" : endpoint.substr(0, colon);
    const std::string port = endpoint.substr(colon + 1);
    if (!port.empty()) {
        options->port = std::atoi(port.c_str());
    }
}
#endif

} // namespace

ShortUrlCache& ShortUrlCache::instance() {
    static ShortUrlCache cache;
    return cache;
}

ShortUrlCache::ShortUrlCache()
    : enabled_(false), redis_available_(false), bloom_ready_(false), ttl_seconds_(3600),
      ttl_jitter_seconds_(300), bloom_bits_(1048576), bloom_hashes_(7),
      rng_(static_cast<unsigned>(
          std::chrono::steady_clock::now().time_since_epoch().count())) {}

void ShortUrlCache::init(const Config& config) {
    std::lock_guard<std::mutex> guard(mutex_);

    enabled_ = config.redis_enabled;
    redis_available_ = false;
    bloom_ready_ = false;
    ttl_seconds_ = std::max(1, config.cache_ttl_seconds);
    ttl_jitter_seconds_ = std::max(0, config.cache_ttl_jitter_seconds);
    bloom_bits_ = std::max(8, config.bloom_bits);
    bloom_hashes_ = std::max(1, config.bloom_hashes);
    bloom_.reset(bloom_bits_, bloom_hashes_);

    if (!enabled_) {
        return;
    }

#ifdef HAVE_REDIS_PLUS_PLUS
    try {
        sw::redis::ConnectionOptions options;
        fill_redis_options(config.redis_uri, &options);
        options.connect_timeout = std::chrono::milliseconds(config.redis_connect_timeout_ms);
        options.socket_timeout = std::chrono::milliseconds(config.redis_socket_timeout_ms);
        redis_.reset(new sw::redis::Redis(options));
        redis_->ping();
        redis_available_ = true;
    } catch (const sw::redis::Error&) {
        redis_.reset();
        redis_available_ = false;
    }
#endif
}

bool ShortUrlCache::warmup(connection_pool* pool, std::string* error) {
    MYSQL* mysql = nullptr;
    if (pool) {
        mysql = pool->GetConnection();
    }
    if (!mysql) {
        if (error) *error = "mysql connection unavailable";
        return false;
    }

    ShortUrlRepository repo(mysql);
    std::vector<std::string> codes;
    const bool ok = repo.list_active_codes(&codes, error);
    pool->ReleaseConnection(mysql);
    if (!ok) {
        return false;
    }

    for (const std::string& code : codes) {
        bloom_.add(code);
    }
    bloom_ready_ = true;
    return true;
}

ShortUrlCache::CacheStatus
ShortUrlCache::get(const std::string& code, std::string* long_url,
                   std::string* error) {
    if (bloom_ready_ && !bloom_.might_contain(code)) {
        return CacheStatus::Filtered;
    }

    if (!enabled_) {
        return CacheStatus::Unavailable;
    }

#ifdef HAVE_REDIS_PLUS_PLUS
    std::lock_guard<std::mutex> guard(mutex_);
    if (!redis_available_ || !redis_) {
        return CacheStatus::Unavailable;
    }

    try {
        auto value = redis_->get(cache_key(code));
        if (!value) {
            return CacheStatus::Miss;
        }
        if (long_url) {
            *long_url = *value;
        }
        return CacheStatus::Hit;
    } catch (const sw::redis::Error& e) {
        redis_available_ = false;
        if (error) *error = e.what();
        return CacheStatus::Unavailable;
    }
#else
    (void)long_url;
    (void)error;
    return CacheStatus::Unavailable;
#endif
}

bool ShortUrlCache::set(const std::string& code, const std::string& long_url,
                        std::string* error) {
    add_legal_code(code);

    if (!enabled_) {
        return false;
    }

#ifdef HAVE_REDIS_PLUS_PLUS
    std::lock_guard<std::mutex> guard(mutex_);
    if (!redis_available_ || !redis_) {
        return false;
    }

    try {
        redis_->setex(cache_key(code), ttl_with_jitter(), long_url);
        return true;
    } catch (const sw::redis::Error& e) {
        redis_available_ = false;
        if (error) *error = e.what();
        return false;
    }
#else
    (void)long_url;
    (void)error;
    return false;
#endif
}

bool ShortUrlCache::erase(const std::string& code, std::string* error) {
    if (!enabled_) {
        return false;
    }

#ifdef HAVE_REDIS_PLUS_PLUS
    std::lock_guard<std::mutex> guard(mutex_);
    if (!redis_available_ || !redis_) {
        return false;
    }

    try {
        redis_->del(cache_key(code));
        return true;
    } catch (const sw::redis::Error& e) {
        redis_available_ = false;
        if (error) *error = e.what();
        return false;
    }
#else
    (void)code;
    (void)error;
    return false;
#endif
}

void ShortUrlCache::add_legal_code(const std::string& code) {
    bloom_.add(code);
}

std::shared_ptr<std::mutex> ShortUrlCache::rebuild_mutex(const std::string& code) {
    return singleflight_.mutex_for(code);
}

bool ShortUrlCache::enabled() const {
    return enabled_;
}

bool ShortUrlCache::redis_available() const {
    return redis_available_;
}

int ShortUrlCache::ttl_with_jitter() {
    if (ttl_jitter_seconds_ <= 0) {
        return ttl_seconds_;
    }
    std::uniform_int_distribution<int> dist(0, ttl_jitter_seconds_);
    return ttl_seconds_ + dist(rng_);
}

std::string ShortUrlCache::cache_key(const std::string& code) const {
    return "shorturl:" + code;
}
