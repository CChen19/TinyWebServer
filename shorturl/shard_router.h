#ifndef SHORTURL_SHARD_ROUTER_H
#define SHORTURL_SHARD_ROUTER_H

#include "../config/config.h"
#include <cstdint>
#include <string>
#include <vector>

struct ShardRoute {
    int database_index = 0;
    int table_index = 0;
    std::string database_name;
    std::string table_name;
    std::string qualified_table;
};

class ShardRouter {
public:
    static ShardRouter& instance();

    void init(const Config& config);
    bool enabled() const;
    ShardRoute route_for_code(const std::string& short_code) const;
    std::vector<ShardRoute> all_routes() const;
    uint64_t hash_code(const std::string& short_code) const;

private:
    ShardRouter();

    std::string format_name(const std::string& prefix, int index) const;
    std::string qualified(const std::string& database, const std::string& table) const;

    bool enabled_;
    std::string database_prefix_;
    std::string table_prefix_;
    int database_count_;
    int table_count_;
};

#endif
