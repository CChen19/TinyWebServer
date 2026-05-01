#include "bloom_filter.h"
#include <algorithm>

BloomFilter::BloomFilter()
    : bit_count_(0), hash_count_(0), inserted_count_(0) {}

void BloomFilter::reset(size_t bit_count, size_t hash_count) {
    std::lock_guard<std::mutex> guard(mutex_);
    bit_count_ = std::max<size_t>(bit_count, 8);
    hash_count_ = std::max<size_t>(hash_count, 1);
    bits_.assign((bit_count_ + 7) / 8, 0);
    inserted_count_ = 0;
}

void BloomFilter::add(const std::string& value) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (bit_count_ == 0 || hash_count_ == 0) {
        return;
    }

    const uint64_t h1 = hash(value, 0x9e3779b185ebca87ULL);
    const uint64_t h2 = hash(value, 0xc2b2ae3d27d4eb4fULL);
    for (size_t i = 0; i < hash_count_; ++i) {
        set_bit((h1 + i * h2) % bit_count_);
    }
    ++inserted_count_;
}

bool BloomFilter::might_contain(const std::string& value) const {
    std::lock_guard<std::mutex> guard(mutex_);
    if (bit_count_ == 0 || hash_count_ == 0) {
        return true;
    }

    const uint64_t h1 = hash(value, 0x9e3779b185ebca87ULL);
    const uint64_t h2 = hash(value, 0xc2b2ae3d27d4eb4fULL);
    for (size_t i = 0; i < hash_count_; ++i) {
        if (!get_bit((h1 + i * h2) % bit_count_)) {
            return false;
        }
    }
    return true;
}

size_t BloomFilter::inserted_count() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return inserted_count_;
}

uint64_t BloomFilter::hash(const std::string& value, uint64_t seed) const {
    uint64_t h = 1469598103934665603ULL ^ seed;
    for (unsigned char c : value) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return h;
}

void BloomFilter::set_bit(size_t idx) {
    bits_[idx / 8] |= static_cast<uint8_t>(1U << (idx % 8));
}

bool BloomFilter::get_bit(size_t idx) const {
    return (bits_[idx / 8] & static_cast<uint8_t>(1U << (idx % 8))) != 0;
}
