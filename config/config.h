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

    int close_log;
};

#endif
