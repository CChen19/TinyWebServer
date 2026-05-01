#include "short_url_repository.h"
#include "shard_router.h"
#include <mysql/errmsg.h>
#include <mysql/mysqld_error.h>

namespace {

std::string short_url_table_for_code(const std::string& short_code) {
    ShardRouter& router = ShardRouter::instance();
    if (!router.enabled()) {
        return "short_url";
    }
    return router.route_for_code(short_code).qualified_table;
}

} // namespace

ShortUrlRepository::ShortUrlRepository(MYSQL* mysql) : mysql_(mysql) {}

ShortUrlRepository::CreateStatus
ShortUrlRepository::create(const ShortUrlRecord& record, std::string* error) {
    if (!mysql_) {
        if (error) *error = "mysql connection unavailable";
        return CreateStatus::DbError;
    }

    std::string sql = "INSERT INTO ";
    sql += short_url_table_for_code(record.short_code);
    sql += " ";
    sql +=
        "(id, short_code, long_url, created_at, expire_at) VALUES (";
    sql += std::to_string(record.id);
    sql += ", ";
    sql += quote(record.short_code);
    sql += ", ";
    sql += quote(record.long_url);
    sql += ", NOW(), ";
    sql += record.expire_at.empty() ? "NULL" : quote(record.expire_at);
    sql += ")";

    if (mysql_query(mysql_, sql.c_str()) != 0) {
        if (mysql_errno(mysql_) == ER_DUP_ENTRY) {
            return CreateStatus::DuplicateCode;
        }
        if (error) *error = mysql_error(mysql_);
        return CreateStatus::DbError;
    }

    return CreateStatus::Ok;
}

ShortUrlRepository::FindStatus
ShortUrlRepository::find_long_url(const std::string& code, std::string* long_url,
                                  std::string* error, bool* cacheable) {
    if (!mysql_) {
        if (error) *error = "mysql connection unavailable";
        return FindStatus::DbError;
    }

    std::string sql =
        "SELECT long_url, "
        "IF(expire_at IS NOT NULL AND expire_at <= NOW(), 1, 0) AS expired, "
        "IF(expire_at IS NULL, 1, 0) AS cacheable "
        "FROM ";
    sql += short_url_table_for_code(code);
    sql += " WHERE short_code = ";
    sql += quote(code);
    sql += " LIMIT 1";

    if (mysql_query(mysql_, sql.c_str()) != 0) {
        if (error) *error = mysql_error(mysql_);
        return FindStatus::DbError;
    }

    MYSQL_RES* result = mysql_store_result(mysql_);
    if (!result) {
        if (error) *error = mysql_error(mysql_);
        return FindStatus::DbError;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    if (!row) {
        mysql_free_result(result);
        return FindStatus::NotFound;
    }

    if (row[1] && std::string(row[1]) == "1") {
        mysql_free_result(result);
        return FindStatus::Expired;
    }

    if (long_url) {
        *long_url = row[0] ? row[0] : "";
    }
    if (cacheable) {
        *cacheable = row[2] && std::string(row[2]) == "1";
    }
    mysql_free_result(result);
    return FindStatus::Ok;
}

bool ShortUrlRepository::list_active_codes(std::vector<std::string>* codes,
                                           std::string* error) {
    if (!mysql_) {
        if (error) *error = "mysql connection unavailable";
        return false;
    }

    std::vector<std::string> tables;
    if (ShardRouter::instance().enabled()) {
        for (const ShardRoute& route : ShardRouter::instance().all_routes()) {
            tables.push_back(route.qualified_table);
        }
    } else {
        tables.push_back("short_url");
    }

    for (const std::string& table : tables) {
        std::string sql =
            "SELECT short_code FROM " + table +
            " WHERE expire_at IS NULL OR expire_at > NOW()";

        if (mysql_query(mysql_, sql.c_str()) != 0) {
            if (error) *error = mysql_error(mysql_);
            return false;
        }

        MYSQL_RES* result = mysql_store_result(mysql_);
        if (!result) {
            if (error) *error = mysql_error(mysql_);
            return false;
        }

        MYSQL_ROW row;
        while ((row = mysql_fetch_row(result)) != nullptr) {
            if (row[0] && codes) {
                codes->push_back(row[0]);
            }
        }

        mysql_free_result(result);
    }
    return true;
}

std::string ShortUrlRepository::quote(const std::string& value) const {
    std::string escaped(value.size() * 2 + 1, '\0');
    unsigned long len = mysql_real_escape_string(
        mysql_, &escaped[0], value.c_str(), value.size());
    escaped.resize(len);
    return "'" + escaped + "'";
}
