#include "short_url_handler.h"
#include "../analytics/click_event_producer.h"
#include "../http/router.h"
#include "../shorturl/base62.h"
#include "../shorturl/short_url_cache.h"
#include "../shorturl/short_url_repository.h"
#include "../shorturl/snowflake.h"
#include <nlohmann/json.hpp>
#include <cctype>

namespace {

bool starts_with(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix) == 0;
}

bool valid_long_url(const std::string& url) {
    return url.size() >= 8 && url.size() <= 2048 &&
           (starts_with(url, "http://") || starts_with(url, "https://"));
}

bool valid_expire_at(const std::string& expire_at) {
    if (expire_at.size() != 19) {
        return false;
    }
    for (size_t i = 0; i < expire_at.size(); ++i) {
        if (i == 4 || i == 7) {
            if (expire_at[i] != '-') return false;
        } else if (i == 10) {
            if (expire_at[i] != ' ') return false;
        } else if (i == 13 || i == 16) {
            if (expire_at[i] != ':') return false;
        } else if (!std::isdigit(static_cast<unsigned char>(expire_at[i]))) {
            return false;
        }
    }
    return true;
}

std::string header_or_empty(const HttpRequest& req, const std::string& name) {
    auto it = req.headers.find(name);
    return it == req.headers.end() ? "" : it->second;
}

std::string build_short_url(const HttpRequest& req, const std::string& code) {
    std::string host = header_or_empty(req, "Host");
    if (host.empty()) {
        return "/" + code;
    }

    std::string scheme = header_or_empty(req, "X-Forwarded-Proto");
    if (scheme.empty()) {
        scheme = "http";
    }
    return scheme + "://" + host + "/" + code;
}

void shorten(const HttpRequest& req, HttpResponse& resp) {
    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(req.body);
    } catch (const nlohmann::json::parse_error&) {
        resp.set_status(400);
        resp.set_json({{"error", "invalid json"}});
        return;
    }

    if (!payload.is_object() || !payload.contains("long_url") ||
        !payload["long_url"].is_string()) {
        resp.set_status(400);
        resp.set_json({{"error", "long_url is required"}});
        return;
    }

    const std::string long_url = payload["long_url"].get<std::string>();
    if (!valid_long_url(long_url)) {
        resp.set_status(400);
        resp.set_json({{"error", "long_url must start with http:// or https://"}});
        return;
    }

    std::string expire_at;
    if (payload.contains("expire_at") && !payload["expire_at"].is_null()) {
        if (!payload["expire_at"].is_string()) {
            resp.set_status(400);
            resp.set_json({{"error", "expire_at must be a datetime string"}});
            return;
        }
        expire_at = payload["expire_at"].get<std::string>();
        if (!valid_expire_at(expire_at)) {
            resp.set_status(400);
            resp.set_json({{"error", "expire_at format must be YYYY-MM-DD HH:MM:SS"}});
            return;
        }
    }

    static SnowflakeIdGenerator generator;
    ShortUrlRepository repo(req.mysql);

    std::string db_error;
    for (int i = 0; i < 3; ++i) {
        ShortUrlRecord record;
        record.id = generator.next_id();
        record.short_code = base62_encode(record.id);
        record.long_url = long_url;
        record.expire_at = expire_at;

        ShortUrlRepository::CreateStatus status = repo.create(record, &db_error);
        if (status == ShortUrlRepository::CreateStatus::Ok) {
            if (expire_at.empty()) {
                ShortUrlCache::instance().set(record.short_code, long_url);
            } else {
                ShortUrlCache::instance().add_legal_code(record.short_code);
            }

            resp.set_status(201);
            nlohmann::json body = {
                {"short_code", record.short_code},
                {"short_url", build_short_url(req, record.short_code)},
                {"long_url", long_url}
            };
            body["expire_at"] = expire_at.empty() ? nlohmann::json(nullptr) : nlohmann::json(expire_at);
            resp.set_json(body);
            return;
        }
        if (status != ShortUrlRepository::CreateStatus::DuplicateCode) {
            break;
        }
    }

    resp.set_status(503);
    resp.set_json({{"error", "short url storage unavailable"}, {"detail", db_error}});
}

void redirect(const HttpRequest& req, HttpResponse& resp) {
    auto it = req.params.find("code");
    if (it == req.params.end() || it->second.empty()) {
        resp.set_status(404);
        resp.set_json({{"error", "not found"}});
        return;
    }

    const std::string code = it->second;
    ShortUrlCache& cache = ShortUrlCache::instance();
    std::string long_url;
    std::string cache_error;

    ShortUrlCache::CacheStatus cache_status =
        cache.get(code, &long_url, &cache_error);
    if (cache_status == ShortUrlCache::CacheStatus::Hit) {
        ClickEventProducer::instance().publish_click(req, code);
        resp.set_status(302);
        resp.set_header("Location", long_url);
        resp.set_body("");
        return;
    }
    if (cache_status == ShortUrlCache::CacheStatus::Filtered) {
        resp.set_status(404);
        resp.set_json({{"error", "short url not found"}});
        return;
    }

    std::shared_ptr<std::mutex> rebuild_lock = cache.rebuild_mutex(code);
    std::lock_guard<std::mutex> guard(*rebuild_lock);

    cache_status = cache.get(code, &long_url, &cache_error);
    if (cache_status == ShortUrlCache::CacheStatus::Hit) {
        ClickEventProducer::instance().publish_click(req, code);
        resp.set_status(302);
        resp.set_header("Location", long_url);
        resp.set_body("");
        return;
    }

    ShortUrlRepository repo(req.mysql);
    std::string db_error;
    bool cacheable = false;
    ShortUrlRepository::FindStatus status =
        repo.find_long_url(code, &long_url, &db_error, &cacheable);

    if (status == ShortUrlRepository::FindStatus::Ok) {
        if (cacheable) {
            cache.set(code, long_url);
        } else {
            cache.add_legal_code(code);
        }
        ClickEventProducer::instance().publish_click(req, code);
        resp.set_status(302);
        resp.set_header("Location", long_url);
        resp.set_body("");
        return;
    }
    if (status == ShortUrlRepository::FindStatus::Expired) {
        cache.erase(code);
        resp.set_status(410);
        resp.set_json({{"error", "short url expired"}});
        return;
    }
    if (status == ShortUrlRepository::FindStatus::NotFound) {
        resp.set_status(404);
        resp.set_json({{"error", "short url not found"}});
        return;
    }

    resp.set_status(503);
    resp.set_json({{"error", "short url storage unavailable"}, {"detail", db_error}});
}

} // namespace

void register_short_url_routes() {
    Router::instance().post("/api/shorten", shorten);
    Router::instance().get("/{code}", redirect);
}
