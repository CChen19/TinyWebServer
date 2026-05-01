#ifndef SHORTURL_BASE62_H
#define SHORTURL_BASE62_H

#include <cstdint>
#include <string>

std::string base62_encode(uint64_t value);

#endif
