#ifndef SHORTURL_BLOOM_FILTER_H
#define SHORTURL_BLOOM_FILTER_H

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class BloomFilter {
public:
    BloomFilter();

    void reset(size_t bit_count, size_t hash_count);
    void add(const std::string& value);
    bool might_contain(const std::string& value) const;
    size_t inserted_count() const;

private:
    uint64_t hash(const std::string& value, uint64_t seed) const;
    void set_bit(size_t idx);
    bool get_bit(size_t idx) const;

    mutable std::mutex mutex_;
    std::vector<uint8_t> bits_;
    size_t bit_count_;
    size_t hash_count_;
    size_t inserted_count_;
};

#endif
