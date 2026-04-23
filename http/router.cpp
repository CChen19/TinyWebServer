#include "router.h"
#include <sstream>

Router& Router::instance() {
    static Router r;
    return r;
}

static std::vector<std::string> split_path(const std::string& path) {
    std::vector<std::string> segs;
    std::istringstream ss(path);
    std::string seg;
    while (std::getline(ss, seg, '/')) {
        if (!seg.empty()) segs.push_back(seg);
    }
    return segs;
}

void Router::get(const std::string& pattern, HandlerFunc h) {
    Route r;
    r.method = "GET";
    r.segments = split_path(pattern);
    r.handler = std::move(h);
    routes_.push_back(std::move(r));
}

void Router::post(const std::string& pattern, HandlerFunc h) {
    Route r;
    r.method = "POST";
    r.segments = split_path(pattern);
    r.handler = std::move(h);
    routes_.push_back(std::move(r));
}

bool Router::dispatch(HttpRequest& req, HttpResponse& resp) {
    for (auto& route : routes_) {
        std::unordered_map<std::string, std::string> params;
        if (match(route, req, params)) {
            req.params = std::move(params);
            route.handler(req, resp);
            return true;
        }
    }
    return false;
}

bool Router::match(const Route& r, const HttpRequest& req,
                   std::unordered_map<std::string, std::string>& params) {
    if (r.method != req.method) return false;

    std::vector<std::string> req_segs = split_path(req.path);

    if (req_segs.size() != r.segments.size()) return false;

    for (size_t i = 0; i < r.segments.size(); ++i) {
        const auto& pat = r.segments[i];
        if (pat.size() >= 3 && pat.front() == '{' && pat.back() == '}') {
            params[pat.substr(1, pat.size() - 2)] = req_segs[i];
        } else if (pat != req_segs[i]) {
            return false;
        }
    }
    return true;
}
