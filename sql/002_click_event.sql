CREATE TABLE IF NOT EXISTS click_event (
    event_id VARCHAR(32) NOT NULL,
    short_code VARCHAR(16) NOT NULL,
    clicked_at_ms BIGINT NOT NULL,
    user_agent VARCHAR(512) NULL,
    referer VARCHAR(1024) NULL,
    x_forwarded_for VARCHAR(255) NULL,
    consumed_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (event_id),
    KEY idx_short_code_clicked_at (short_code, clicked_at_ms)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
