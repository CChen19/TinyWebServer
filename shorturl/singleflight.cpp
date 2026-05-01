#include "singleflight.h"

std::shared_ptr<std::mutex> SingleFlight::mutex_for(const std::string& key) {
    std::lock_guard<std::mutex> guard(mutex_);

    auto it = locks_.find(key);
    if (it != locks_.end()) {
        std::shared_ptr<std::mutex> existing = it->second.lock();
        if (existing) {
            return existing;
        }
    }

    std::shared_ptr<std::mutex> created = std::make_shared<std::mutex>();
    locks_[key] = created;
    return created;
}
