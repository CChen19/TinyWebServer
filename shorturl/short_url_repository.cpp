#include "short_url_repository.h"
#include <mysql/errmsg.h>
#include <mysql/mysqld_error.h>

ShortUrlRepository::ShortUrlRepository(MYSQL* mysql) : mysql_(mysql) {}

ShortUrlRepository::CreateStatus
ShortUrlRepository::create(const ShortUrlRecord& record, std::string* error) {
    if (!mysql_) {
        if (error) *error = "mysql connection unavailable";
        return CreateStatus::DbError;
    }

    std::string sql = "INSERT INTO short_url "
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
                                  std::string* error) {
    if (!mysql_) {
        if (error) *error = "mysql connection unavailable";
        return FindStatus::DbError;
    }

    std::string sql =
        "SELECT long_url, "
        "IF(expire_at IS NOT NULL AND expire_at <= NOW(), 1, 0) AS expired "
        "FROM short_url WHERE short_code = ";
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
    mysql_free_result(result);
    return FindStatus::Ok;
}

std::string ShortUrlRepository::quote(const std::string& value) const {
    std::string escaped(value.size() * 2 + 1, '\0');
    unsigned long len = mysql_real_escape_string(
        mysql_, &escaped[0], value.c_str(), value.size());
    escaped.resize(len);
    return "'" + escaped + "'";
}
