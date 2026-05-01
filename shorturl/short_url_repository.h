#ifndef SHORTURL_SHORT_URL_REPOSITORY_H
#define SHORTURL_SHORT_URL_REPOSITORY_H

#include <mysql/mysql.h>
#include <cstdint>
#include <string>

struct ShortUrlRecord {
    uint64_t id = 0;
    std::string short_code;
    std::string long_url;
    std::string expire_at;
};

class ShortUrlRepository {
public:
    enum class CreateStatus {
        Ok,
        DuplicateCode,
        DbError
    };

    enum class FindStatus {
        Ok,
        NotFound,
        Expired,
        DbError
    };

    explicit ShortUrlRepository(MYSQL* mysql);

    CreateStatus create(const ShortUrlRecord& record, std::string* error = nullptr);
    FindStatus find_long_url(const std::string& code, std::string* long_url,
                             std::string* error = nullptr);

private:
    std::string quote(const std::string& value) const;

    MYSQL* mysql_;
};

#endif
