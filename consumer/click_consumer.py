#!/usr/bin/env python3
"""
Consume short URL click events from Kafka and write them to MySQL.

Required packages:
  pip install confluent-kafka mysql-connector-python

The consumer commits offsets only after MySQL has accepted the event. The
`click_event.event_id` primary key makes repeated delivery idempotent.
"""

import json
import os
import signal
import sys
import threading
import time
from json import JSONDecodeError
from http.server import BaseHTTPRequestHandler, HTTPServer
from typing import Any, Dict

import mysql.connector
from confluent_kafka import Consumer, KafkaException


RUNNING = True


class ConsumerMetrics:
    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.events_consumed_total = 0
        self.events_inserted_total = 0
        self.events_invalid_total = 0
        self.events_failed_total = 0
        self.kafka_lag = 0
        self.last_commit_ts = 0.0

    def inc(self, name: str) -> None:
        with self.lock:
            setattr(self, name, getattr(self, name) + 1)

    def set_lag(self, value: int) -> None:
        with self.lock:
            self.kafka_lag = max(0, value)

    def mark_commit(self) -> None:
        with self.lock:
            self.last_commit_ts = time.time()

    def render(self) -> str:
        with self.lock:
            lines = [
                "# HELP shorturl_click_consumer_events_total Click consumer events by result.",
                "# TYPE shorturl_click_consumer_events_total counter",
                f'shorturl_click_consumer_events_total{{result="consumed"}} {self.events_consumed_total}',
                f'shorturl_click_consumer_events_total{{result="inserted"}} {self.events_inserted_total}',
                f'shorturl_click_consumer_events_total{{result="invalid"}} {self.events_invalid_total}',
                f'shorturl_click_consumer_events_total{{result="failed"}} {self.events_failed_total}',
                "# HELP shorturl_kafka_consumer_lag Kafka consumer lag across assigned partitions.",
                "# TYPE shorturl_kafka_consumer_lag gauge",
                f"shorturl_kafka_consumer_lag {self.kafka_lag}",
                "# HELP shorturl_click_consumer_last_commit_timestamp_seconds Last successful offset commit timestamp.",
                "# TYPE shorturl_click_consumer_last_commit_timestamp_seconds gauge",
                f"shorturl_click_consumer_last_commit_timestamp_seconds {self.last_commit_ts:.0f}",
                "",
            ]
        return "\n".join(lines)


METRICS = ConsumerMetrics()


def stop(_signum, _frame):
    global RUNNING
    RUNNING = False


class MetricsHandler(BaseHTTPRequestHandler):
    def do_GET(self) -> None:
        if self.path != "/metrics":
            self.send_response(404)
            self.end_headers()
            return
        body = METRICS.render().encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/plain; version=0.0.4; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, _format: str, *_args: Any) -> None:
        return


def start_metrics_server() -> None:
    port = int(os.getenv("METRICS_PORT", "9108"))
    server = HTTPServer((os.getenv("METRICS_HOST", "127.0.0.1"), port), MetricsHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()


def log_json(event: str, **fields: Any) -> None:
    record = {"event": event, **fields}
    print(json.dumps(record, ensure_ascii=False, separators=(",", ":")), flush=True)


def mysql_conn():
    return mysql.connector.connect(
        host=os.getenv("MYSQL_HOST", "127.0.0.1"),
        port=int(os.getenv("MYSQL_PORT", "3306")),
        user=os.getenv("MYSQL_USER", "shorturl"),
        password=os.getenv("MYSQL_PASSWORD", "shorturl"),
        database=os.getenv("MYSQL_DATABASE", "shorturl"),
    )


def insert_event(conn, event: Dict[str, Any]) -> None:
    sql = """
        INSERT INTO click_event
            (event_id, short_code, clicked_at_ms, user_agent, referer, x_forwarded_for)
        VALUES (%s, %s, %s, %s, %s, %s)
        ON DUPLICATE KEY UPDATE event_id = event_id
    """
    values = (
        event["event_id"],
        event["short_code"],
        int(event["clicked_at_ms"]),
        event.get("user_agent") or None,
        event.get("referer") or None,
        event.get("x_forwarded_for") or None,
    )
    cursor = conn.cursor()
    cursor.execute(sql, values)
    conn.commit()
    cursor.close()


def update_lag(consumer: Consumer) -> None:
    total = 0
    try:
        positions = consumer.position(consumer.assignment())
        for tp in positions:
            if tp.offset < 0:
                continue
            _low, high = consumer.get_watermark_offsets(tp, timeout=1.0)
            total += max(0, high - tp.offset)
        METRICS.set_lag(total)
    except KafkaException:
        return


def main() -> int:
    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)
    start_metrics_server()

    consumer = Consumer({
        "bootstrap.servers": os.getenv("KAFKA_BROKERS", "127.0.0.1:9092"),
        "group.id": os.getenv("KAFKA_GROUP_ID", "shorturl-click-writer"),
        "enable.auto.commit": False,
        "auto.offset.reset": "earliest",
    })
    topic = os.getenv("KAFKA_CLICK_TOPIC", "shorturl.clicks")
    consumer.subscribe([topic])

    conn = mysql_conn()
    try:
        while RUNNING:
            msg = consumer.poll(1.0)
            if msg is None:
                update_lag(consumer)
                continue
            if msg.error():
                raise KafkaException(msg.error())

            METRICS.inc("events_consumed_total")
            try:
                event = json.loads(msg.value().decode("utf-8"))
            except JSONDecodeError as exc:
                METRICS.inc("events_invalid_total")
                log_json("click_event_invalid", error=str(exc))
                consumer.commit(message=msg, asynchronous=False)
                METRICS.mark_commit()
                continue

            try:
                insert_event(conn, event)
                METRICS.inc("events_inserted_total")
                consumer.commit(message=msg, asynchronous=False)
                METRICS.mark_commit()
                update_lag(consumer)
                log_json(
                    "click_event_committed",
                    event_id=event.get("event_id"),
                    short_code=event.get("short_code"),
                    partition=msg.partition(),
                    offset=msg.offset(),
                )
            except Exception as exc:
                METRICS.inc("events_failed_total")
                log_json("click_event_failed", error=str(exc), file="consumer/click_consumer.py")
                raise
    finally:
        conn.close()
        consumer.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
