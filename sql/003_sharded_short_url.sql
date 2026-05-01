CREATE DATABASE IF NOT EXISTS shorturl_00 DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE DATABASE IF NOT EXISTS shorturl_01 DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE DATABASE IF NOT EXISTS shorturl_02 DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE DATABASE IF NOT EXISTS shorturl_03 DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

GRANT ALL PRIVILEGES ON shorturl_00.* TO 'shorturl'@'localhost';
GRANT ALL PRIVILEGES ON shorturl_01.* TO 'shorturl'@'localhost';
GRANT ALL PRIVILEGES ON shorturl_02.* TO 'shorturl'@'localhost';
GRANT ALL PRIVILEGES ON shorturl_03.* TO 'shorturl'@'localhost';
GRANT ALL PRIVILEGES ON shorturl_00.* TO 'shorturl'@'127.0.0.1';
GRANT ALL PRIVILEGES ON shorturl_01.* TO 'shorturl'@'127.0.0.1';
GRANT ALL PRIVILEGES ON shorturl_02.* TO 'shorturl'@'127.0.0.1';
GRANT ALL PRIVILEGES ON shorturl_03.* TO 'shorturl'@'127.0.0.1';
FLUSH PRIVILEGES;

CREATE TABLE IF NOT EXISTS shorturl_00.short_url_00 LIKE shorturl.short_url;
CREATE TABLE IF NOT EXISTS shorturl_00.short_url_01 LIKE shorturl.short_url;
CREATE TABLE IF NOT EXISTS shorturl_00.short_url_02 LIKE shorturl.short_url;
CREATE TABLE IF NOT EXISTS shorturl_00.short_url_03 LIKE shorturl.short_url;

CREATE TABLE IF NOT EXISTS shorturl_01.short_url_00 LIKE shorturl.short_url;
CREATE TABLE IF NOT EXISTS shorturl_01.short_url_01 LIKE shorturl.short_url;
CREATE TABLE IF NOT EXISTS shorturl_01.short_url_02 LIKE shorturl.short_url;
CREATE TABLE IF NOT EXISTS shorturl_01.short_url_03 LIKE shorturl.short_url;

CREATE TABLE IF NOT EXISTS shorturl_02.short_url_00 LIKE shorturl.short_url;
CREATE TABLE IF NOT EXISTS shorturl_02.short_url_01 LIKE shorturl.short_url;
CREATE TABLE IF NOT EXISTS shorturl_02.short_url_02 LIKE shorturl.short_url;
CREATE TABLE IF NOT EXISTS shorturl_02.short_url_03 LIKE shorturl.short_url;

CREATE TABLE IF NOT EXISTS shorturl_03.short_url_00 LIKE shorturl.short_url;
CREATE TABLE IF NOT EXISTS shorturl_03.short_url_01 LIKE shorturl.short_url;
CREATE TABLE IF NOT EXISTS shorturl_03.short_url_02 LIKE shorturl.short_url;
CREATE TABLE IF NOT EXISTS shorturl_03.short_url_03 LIKE shorturl.short_url;
