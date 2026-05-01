#include "snowflake.h"
#include <chrono>
#include <stdexcept>
#include <thread>

uint64_t SnowflakeIdGenerator::next_id() {
    std::lock_guard<std::mutex> guard(mutex_);

    uint64_t now = current_seconds();
    if (now < last_second_) {
        now = last_second_;
    }

    if (now == last_second_) {
        if (sequence_ >= kMaxSequence) {
            now = wait_next_second(last_second_);
            sequence_ = 0;
        } else {
            ++sequence_;
        }
    } else {
        sequence_ = 0;
    }

    if (now > kMaxTimestampDelta) {
        throw std::runtime_error("snowflake timestamp overflow");
    }

    last_second_ = now;
    return (now << kSequenceBits) | sequence_;
}

uint64_t SnowflakeIdGenerator::current_seconds() const {
    using namespace std::chrono;
    const uint64_t unix_seconds =
        duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
    if (unix_seconds <= kEpochSeconds) {
        return 0;
    }
    return unix_seconds - kEpochSeconds;
}

uint64_t SnowflakeIdGenerator::wait_next_second(uint64_t last_second) const {
    uint64_t now = current_seconds();
    while (now <= last_second) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        now = current_seconds();
    }
    return now;
}
