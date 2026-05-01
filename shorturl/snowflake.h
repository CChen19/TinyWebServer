#ifndef SHORTURL_SNOWFLAKE_H
#define SHORTURL_SNOWFLAKE_H

#include <cstdint>
#include <mutex>

class SnowflakeIdGenerator {
public:
    uint64_t next_id();

private:
    static const uint64_t kEpochSeconds = 1767225600ULL; // 2026-01-01 00:00:00 UTC
    static const uint64_t kSequenceBits = 12;
    static const uint64_t kMaxSequence = (1ULL << kSequenceBits) - 1;
    static const uint64_t kMaxTimestampDelta = (1ULL << 29) - 1;

    uint64_t current_seconds() const;
    uint64_t wait_next_second(uint64_t last_second) const;

    std::mutex mutex_;
    uint64_t last_second_ = 0;
    uint64_t sequence_ = 0;
};

#endif
