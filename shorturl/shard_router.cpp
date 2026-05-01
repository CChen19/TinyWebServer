#include "shard_router.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

ShardRouter& ShardRouter::instance() {
    static ShardRouter router;
    return router;
}

ShardRouter::ShardRouter()
    : enabled_(false), database_prefix_("shorturl_"),
      table_prefix_("short_url_"), database_count_(4), table_count_(4) {}

void ShardRouter::init(const Config& config) {
    enabled_ = config.sharding_enabled;
    database_prefix_ = config.shard_database_prefix;
    table_prefix_ = config.shard_table_prefix;
    database_count_ = std::max(1, config.shard_database_count);
    table_count_ = std::max(1, config.shard_table_count);
}

bool ShardRouter::enabled() const {
    return enabled_;
}

ShardRoute ShardRouter::route_for_code(const std::string& short_code) const {
    ShardRoute route;
    const uint64_t hash = hash_code(short_code);
    const uint64_t bucket_count =
        static_cast<uint64_t>(database_count_) * static_cast<uint64_t>(table_count_);
    const uint64_t bucket = bucket_count == 0 ? 0 : hash % bucket_count;

    route.database_index = static_cast<int>(bucket / table_count_);
    route.table_index = static_cast<int>(bucket % table_count_);
    route.database_name = format_name(database_prefix_, route.database_index);
    route.table_name = format_name(table_prefix_, route.table_index);
    route.qualified_table = qualified(route.database_name, route.table_name);
    return route;
}

std::vector<ShardRoute> ShardRouter::all_routes() const {
    std::vector<ShardRoute> routes;
    routes.reserve(database_count_ * table_count_);
    for (int db = 0; db < database_count_; ++db) {
        for (int table = 0; table < table_count_; ++table) {
            ShardRoute route;
            route.database_index = db;
            route.table_index = table;
            route.database_name = format_name(database_prefix_, db);
            route.table_name = format_name(table_prefix_, table);
            route.qualified_table = qualified(route.database_name, route.table_name);
            routes.push_back(route);
        }
    }
    return routes;
}

uint64_t ShardRouter::hash_code(const std::string& short_code) const {
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char c : short_code) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string ShardRouter::format_name(const std::string& prefix, int index) const {
    std::ostringstream oss;
    oss << prefix << std::setw(2) << std::setfill('0') << index;
    return oss.str();
}

std::string ShardRouter::qualified(const std::string& database,
                                   const std::string& table) const {
    return "`" + database + "`.`" + table + "`";
}
