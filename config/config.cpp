#include "config.h"
#include <yaml-cpp/yaml.h>
#include <cstdio>

Config::Config()
    : port(9006), thread_num(8), trig_mode(0), opt_linger(false), actor_model(0),
      log_enabled(true), log_async(false), log_path("./ServerLog"),
      mysql_host("localhost"), mysql_port(3306), mysql_user("shorturl"),
      mysql_password("shorturl"), mysql_database("shorturl"), mysql_pool_size(8),
      redis_enabled(true), redis_uri("tcp://127.0.0.1:6379"),
      redis_connect_timeout_ms(200), redis_socket_timeout_ms(200),
      cache_ttl_seconds(3600), cache_ttl_jitter_seconds(300),
      bloom_bits(1048576), bloom_hashes(7),
      kafka_enabled(true), kafka_brokers("127.0.0.1:9092"),
      kafka_click_topic("shorturl.clicks"), kafka_message_timeout_ms(3000),
      kafka_linger_ms(5), kafka_retries(3),
      sharding_enabled(true), shard_database_prefix("shorturl_"),
      shard_table_prefix("short_url_"), shard_database_count(4),
      shard_table_count(4),
      close_log(0)
{}

bool Config::load(const std::string& path)
{
    try {
        YAML::Node cfg = YAML::LoadFile(path);

        if (cfg["server"]) {
            auto s = cfg["server"];
            if (s["port"])        port        = s["port"].as<int>();
            if (s["thread_num"])  thread_num  = s["thread_num"].as<int>();
            if (s["trig_mode"])   trig_mode   = s["trig_mode"].as<int>();
            if (s["opt_linger"])  opt_linger  = s["opt_linger"].as<bool>();
            if (s["actor_model"]) actor_model = s["actor_model"].as<int>();
        }

        if (cfg["log"]) {
            auto l = cfg["log"];
            if (l["enabled"]) {
                log_enabled = l["enabled"].as<bool>();
                close_log = log_enabled ? 0 : 1;
            }
            if (l["async"]) log_async = l["async"].as<bool>();
            if (l["path"])  log_path  = l["path"].as<std::string>();
        }

        if (cfg["mysql"]) {
            auto m = cfg["mysql"];
            if (m["host"])      mysql_host     = m["host"].as<std::string>();
            if (m["port"])      mysql_port     = m["port"].as<int>();
            if (m["user"])      mysql_user     = m["user"].as<std::string>();
            if (m["password"])  mysql_password = m["password"].as<std::string>();
            if (m["database"])  mysql_database = m["database"].as<std::string>();
            if (m["pool_size"]) mysql_pool_size = m["pool_size"].as<int>();
        }

        if (cfg["redis"]) {
            auto r = cfg["redis"];
            if (r["enabled"])            redis_enabled            = r["enabled"].as<bool>();
            if (r["uri"])                redis_uri                = r["uri"].as<std::string>();
            if (r["connect_timeout_ms"]) redis_connect_timeout_ms = r["connect_timeout_ms"].as<int>();
            if (r["socket_timeout_ms"])  redis_socket_timeout_ms  = r["socket_timeout_ms"].as<int>();
        }

        if (cfg["cache"]) {
            auto c = cfg["cache"];
            if (c["ttl_seconds"])        cache_ttl_seconds        = c["ttl_seconds"].as<int>();
            if (c["ttl_jitter_seconds"]) cache_ttl_jitter_seconds = c["ttl_jitter_seconds"].as<int>();
            if (c["bloom_bits"])         bloom_bits               = c["bloom_bits"].as<int>();
            if (c["bloom_hashes"])       bloom_hashes             = c["bloom_hashes"].as<int>();
        }

        if (cfg["kafka"]) {
            auto k = cfg["kafka"];
            if (k["enabled"])            kafka_enabled            = k["enabled"].as<bool>();
            if (k["brokers"])            kafka_brokers            = k["brokers"].as<std::string>();
            if (k["click_topic"])        kafka_click_topic        = k["click_topic"].as<std::string>();
            if (k["message_timeout_ms"]) kafka_message_timeout_ms = k["message_timeout_ms"].as<int>();
            if (k["linger_ms"])          kafka_linger_ms          = k["linger_ms"].as<int>();
            if (k["retries"])            kafka_retries            = k["retries"].as<int>();
        }

        if (cfg["sharding"]) {
            auto sh = cfg["sharding"];
            if (sh["enabled"])         sharding_enabled     = sh["enabled"].as<bool>();
            if (sh["database_prefix"]) shard_database_prefix = sh["database_prefix"].as<std::string>();
            if (sh["table_prefix"])    shard_table_prefix    = sh["table_prefix"].as<std::string>();
            if (sh["database_count"])  shard_database_count  = sh["database_count"].as<int>();
            if (sh["table_count"])     shard_table_count     = sh["table_count"].as<int>();
        }

        return true;
    } catch (const YAML::Exception& e) {
        fprintf(stderr, "Failed to load config '%s': %s\n", path.c_str(), e.what());
        return false;
    }
}
