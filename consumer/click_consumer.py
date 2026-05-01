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
from json import JSONDecodeError
from typing import Any, Dict

import mysql.connector
from confluent_kafka import Consumer, KafkaException


RUNNING = True


def stop(_signum, _frame):
    global RUNNING
    RUNNING = False


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


def main() -> int:
    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)

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
                continue
            if msg.error():
                raise KafkaException(msg.error())

            try:
                event = json.loads(msg.value().decode("utf-8"))
            except JSONDecodeError as exc:
                print(f"skip invalid click event JSON: {exc}", file=sys.stderr)
                consumer.commit(message=msg, asynchronous=False)
                continue

            insert_event(conn, event)
            consumer.commit(message=msg, asynchronous=False)
    finally:
        conn.close()
        consumer.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
