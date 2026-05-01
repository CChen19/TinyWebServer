#ifndef CONFIG_H
#define CONFIG_H

#include <string>

class Config
{
public:
    Config();

    bool load(const std::string& path);

    int port;
    int thread_num;
    int trig_mode;
    bool opt_linger;
    int actor_model;

    bool log_enabled;
    bool log_async;
    std::string log_path;

    std::string mysql_host;
    int mysql_port;
    std::string mysql_user;
    std::string mysql_password;
    std::string mysql_database;
    int mysql_pool_size;

    bool redis_enabled;
    std::string redis_uri;
    int redis_connect_timeout_ms;
    int redis_socket_timeout_ms;
    int cache_ttl_seconds;
    int cache_ttl_jitter_seconds;
    int bloom_bits;
    int bloom_hashes;

    bool kafka_enabled;
    std::string kafka_brokers;
    std::string kafka_click_topic;
    int kafka_message_timeout_ms;
    int kafka_linger_ms;
    int kafka_retries;

    bool sharding_enabled;
    std::string shard_database_prefix;
    std::string shard_table_prefix;
    int shard_database_count;
    int shard_table_count;

    int close_log;
};

#endif
