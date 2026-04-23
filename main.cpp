#include "./config/config.h"
#include "webserver.h"
#include "./handler/health_handler.h"

int main(int argc, char *argv[])
{
    Config config;

    std::string config_path = "config/config.yaml";
    if (argc > 1)
        config_path = argv[1];

    if (!config.load(config_path))
        fprintf(stderr, "Config load failed, using defaults\n");

    register_health_routes();

    WebServer server;
    server.init(config);

    server.log_write();
    server.sql_pool();
    server.thread_pool();
    server.trig_mode();
    server.eventListen();
    server.eventLoop();

    return 0;
}
