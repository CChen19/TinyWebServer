#include "metrics_handler.h"
#include "../http/router.h"
#include "../observability/metrics_registry.h"

void register_metrics_routes() {
    Router::instance().get("/metrics", [](const HttpRequest&, HttpResponse& resp) {
        resp.set_status(200);
        resp.set_header("Content-Type", "text/plain; version=0.0.4; charset=utf-8");
        resp.set_body(MetricsRegistry::instance().render_prometheus());
    });
}
