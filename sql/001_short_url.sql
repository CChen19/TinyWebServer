CREATE TABLE IF NOT EXISTS short_url (
    id BIGINT UNSIGNED NOT NULL,
    short_code VARCHAR(16) NOT NULL,
    long_url TEXT NOT NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expire_at DATETIME NULL DEFAULT NULL,
    PRIMARY KEY (id),
    UNIQUE KEY uk_short_code (short_code),
    KEY idx_expire_at (expire_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
