#include "config.h"
#include <yaml-cpp/yaml.h>
#include <cstdio>

Config::Config()
    : port(9006), thread_num(8), trig_mode(0), opt_linger(false), actor_model(0),
      log_enabled(true), log_async(false), log_path("./ServerLog"),
      mysql_host("localhost"), mysql_port(3306), mysql_user("root"),
      mysql_password("root"), mysql_database("shorturl"), mysql_pool_size(8),
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

        return true;
    } catch (const YAML::Exception& e) {
        fprintf(stderr, "Failed to load config '%s': %s\n", path.c_str(), e.what());
        return false;
    }
}
