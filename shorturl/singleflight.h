#ifndef SHORTURL_SINGLEFLIGHT_H
#define SHORTURL_SINGLEFLIGHT_H

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

class SingleFlight {
public:
    std::shared_ptr<std::mutex> mutex_for(const std::string& key);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, std::weak_ptr<std::mutex>> locks_;
};

#endif
