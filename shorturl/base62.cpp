#include "base62.h"
#include <algorithm>

std::string base62_encode(uint64_t value) {
    static const char alphabet[] =
        "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

    if (value == 0) {
        return "0";
    }

    std::string out;
    while (value > 0) {
        out.push_back(alphabet[value % 62]);
        value /= 62;
    }
    std::reverse(out.begin(), out.end());
    return out;
}
