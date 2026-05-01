# TinyWebServer Short URL

A production-style C++ short URL service evolved from the classic TinyWebServer project.

It keeps the original epoll-based networking core and rebuilds the application layer around REST APIs, Redis caching, Kafka click events, sharded MySQL storage, Prometheus metrics, and structured logs.

## Architecture

```mermaid
flowchart LR
    Client[Client] --> Server[C++ epoll HTTP Server]
    Server --> Router[REST Router]

    Router -->|POST /api/shorten| Shorten[Shorten Handler]
    Shorten --> Snowflake[Snowflake ID]
    Snowflake --> Base62[Base62 Code]
    Base62 --> ShardRouter[Shard Router]
    ShardRouter --> Shards[(MySQL Shards<br/>4 DB x 4 Tables)]
    Shorten --> Redis[(Redis Cache)]
    Shorten --> Metrics[Prometheus Metrics]

    Router -->|GET /{code}| Redirect[Redirect Handler]
    Redirect --> Bloom[Bloom Filter]
    Bloom --> Redis
    Redis -->|hit| Redirect
    Redis -->|miss| SingleFlight[Singleflight Rebuild]
    SingleFlight --> ShardRouter
    Redirect -->|302| Client
    Redirect --> Producer[Kafka Producer]

    Producer --> Kafka[(Kafka<br/>shorturl.clicks)]
    Kafka --> Consumer[Python Click Consumer]
    Consumer --> ClickDB[(MySQL click_event)]
    Consumer --> LagMetrics[Consumer /metrics<br/>Kafka Lag]

    Server --> AccessLog[JSONL Access Log]
    Server --> Metrics
```

## Features

- `POST /api/shorten` creates short URLs.
- `GET /{code}` returns a `302` redirect.
- Snowflake ID to Base62 short code generation.
- Redis Cache-Aside with Bloom Filter, singleflight rebuild, and TTL jitter.
- Kafka async click pipeline with idempotent consumer writes.
- Thin C++ sharding router over MySQL: `short_code` hash to `4 DB x 4 tables`.
- Prometheus metrics for QPS, latency, cache hit ratio, Kafka publish status, and consumer lag.
- JSONL structured access logs.

## Repository Layout

```text
analytics/       Kafka click producer
consumer/        Python click consumer with Kafka lag metrics
handler/         /health, /metrics, and short URL handlers
http/            HTTP parsing, routing, and response serialization
observability/   Prometheus metrics registry and structured access logs
shorturl/        Base62, Snowflake, Redis cache, sharding router, repository
sql/             MySQL schema and sharding initialization scripts
docs/            Chinese phase-by-phase design notes and validation records
config/          YAML configuration
```

## Quick Start

Install dependencies on Ubuntu/WSL:

```bash
sudo apt install -y \
  build-essential cmake libmysqlclient-dev libyaml-cpp-dev \
  mysql-server mysql-client redis-server libhiredis-dev librdkafka-dev \
  python3-confluent-kafka python3-mysql.connector
```

Install `redis-plus-plus`:

```bash
cd /tmp
git clone https://github.com/sewenew/redis-plus-plus.git
cd redis-plus-plus
mkdir -p build && cd build
cmake .. -DREDIS_PLUS_PLUS_CXX_STANDARD=14
make -j
sudo make install
sudo ldconfig
```

Initialize MySQL:

```bash
sudo service mysql start

sudo mysql <<'SQL'
CREATE DATABASE IF NOT EXISTS shorturl DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER IF NOT EXISTS 'shorturl'@'localhost' IDENTIFIED BY 'shorturl';
CREATE USER IF NOT EXISTS 'shorturl'@'127.0.0.1' IDENTIFIED BY 'shorturl';
GRANT ALL PRIVILEGES ON shorturl.* TO 'shorturl'@'localhost';
GRANT ALL PRIVILEGES ON shorturl.* TO 'shorturl'@'127.0.0.1';
FLUSH PRIVILEGES;
SQL

mysql -h127.0.0.1 -ushorturl -pshorturl shorturl < sql/001_short_url.sql
mysql -h127.0.0.1 -ushorturl -pshorturl shorturl < sql/002_click_event.sql
sudo mysql < sql/003_sharded_short_url.sql
```

Start Redis:

```bash
sudo service redis-server start
redis-cli ping
```

Start local Kafka:

```bash
docker run -d --name tinywebserver-kafka -p 9092:9092 \
  -e KAFKA_NODE_ID=1 \
  -e KAFKA_PROCESS_ROLES=broker,controller \
  -e KAFKA_CONTROLLER_QUORUM_VOTERS=1@localhost:9093 \
  -e KAFKA_LISTENERS=PLAINTEXT://:9092,CONTROLLER://:9093 \
  -e KAFKA_ADVERTISED_LISTENERS=PLAINTEXT://127.0.0.1:9092 \
  -e KAFKA_LISTENER_SECURITY_PROTOCOL_MAP=CONTROLLER:PLAINTEXT,PLAINTEXT:PLAINTEXT \
  -e KAFKA_CONTROLLER_LISTENER_NAMES=CONTROLLER \
  -e KAFKA_INTER_BROKER_LISTENER_NAME=PLAINTEXT \
  -e KAFKA_OFFSETS_TOPIC_REPLICATION_FACTOR=1 \
  -e KAFKA_TRANSACTION_STATE_LOG_REPLICATION_FACTOR=1 \
  -e KAFKA_TRANSACTION_STATE_LOG_MIN_ISR=1 \
  apache/kafka:3.7.0

docker exec tinywebserver-kafka /opt/kafka/bin/kafka-topics.sh \
  --bootstrap-server 127.0.0.1:9092 \
  --create --if-not-exists \
  --topic shorturl.clicks \
  --partitions 3 \
  --replication-factor 1
```

Build and run:

```bash
cmake -S . -B build-linux -DCMAKE_CXX_COMPILER=/usr/bin/g++
cmake --build build-linux

./build-linux/server config/config.yaml
```

Run the click consumer:

```bash
python3 consumer/click_consumer.py
```

## API

Health check:

```bash
curl http://127.0.0.1:9006/health
```

Create a short URL:

```bash
curl -X POST http://127.0.0.1:9006/api/shorten \
  -H 'Content-Type: application/json' \
  -d '{"long_url":"https://example.com"}'
```

Redirect:

```bash
curl -i http://127.0.0.1:9006/<short_code>
```

Metrics:

```bash
curl http://127.0.0.1:9006/metrics
curl http://127.0.0.1:9108/metrics
```

Access logs:

```bash
tail -f logs/access.jsonl
```

## Key Metrics

- `shorturl_http_requests_total`: HTTP request count for QPS.
- `shorturl_http_request_duration_seconds_bucket`: latency histogram for P99.
- `shorturl_cache_requests_total`: Redis hit, miss, filtered, and unavailable counts.
- `shorturl_kafka_publish_total`: Kafka producer success and failure counts.
- `shorturl_kafka_consumer_lag`: click consumer lag.

## Documentation

The detailed phase documents are intentionally kept in Chinese:

- [Phase 1: single-node short URL and baseline benchmark](docs/phase1_baseline.md)
- [Phase 2: Redis cache consistency](docs/phase2_cache_consistency.md)
- [Phase 3: Kafka reliable delivery](docs/phase3_kafka_delivery.md)
- [Phase 4: MySQL sharding](docs/phase4_sharding.md)
- [Phase 5: observability](docs/phase5_observability.md)

## Credits

Original project: [qinguoyi/TinyWebServer](https://github.com/qinguoyi/TinyWebServer)

Reference: *Linux High Performance Server Programming*, You Shuang.
