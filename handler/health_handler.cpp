#include "health_handler.h"
#include "../http/router.h"
#include <nlohmann/json.hpp>

void register_health_routes() {
    Router::instance().get("/health", [](const HttpRequest&, HttpResponse& resp) {
        resp.set_status(200);
        resp.set_json({{"status", "ok"}, {"version", "0.1.0"}});
    });
}
