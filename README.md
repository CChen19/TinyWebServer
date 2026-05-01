TinyWebServer Short URL
=======================

基于 [qinguoyi/TinyWebServer](https://github.com/qinguoyi/TinyWebServer) 演进的 C++ 短链服务。项目保留原来的线程池、非阻塞 socket、epoll、Reactor/Proactor 网络模型，在业务层重构为 RESTful 短链系统。

## 能力概览

- `POST /api/shorten`：创建短链
- `GET /{code}`：302 跳转
- Redis Cache-Aside：Bloom Filter 防穿透、singleflight 防击穿、TTL jitter 防雪崩
- Kafka 点击事件异步化：跳转主链路只投递事件，不同步写点击表
- 自研薄分片路由：`short_code` hash 到 `4 库 x 4 表`
- Prometheus 指标：QPS、延迟直方图、缓存命中率、Kafka producer、consumer lag
- JSONL 结构化访问日志

## 架构

```text
client
  -> C++ epoll HTTP server
  -> Router
      POST /api/shorten -> Snowflake -> Base62 -> ShardRouter -> MySQL shard -> Redis
      GET /{code}       -> Bloom Filter -> Redis -> MySQL shard -> Kafka click topic

consumer/click_consumer.py
  -> Kafka shorturl.clicks
  -> MySQL click_event
  -> /metrics exposes Kafka lag
```

## 项目结构

```text
analytics/       Kafka click producer
consumer/        独立点击事件 consumer
handler/         /health、/metrics、短链接口
http/            请求解析、响应构造、路由
observability/   Prometheus 指标和 JSONL access log
shorturl/        Base62、Snowflake、Redis cache、分片路由、Repository
sql/             MySQL 初始化脚本
docs/            各阶段设计与验证记录
config/          YAML 配置
```

## 依赖

Ubuntu/WSL:

```bash
sudo apt install -y \
  build-essential cmake libmysqlclient-dev libyaml-cpp-dev \
  mysql-server mysql-client redis-server libhiredis-dev librdkafka-dev \
  python3-confluent-kafka python3-mysql.connector
```

`redis-plus-plus` 需要源码安装：

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

## 初始化

### MySQL

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

项目命令统一使用 `127.0.0.1`，避免 MySQL client 默认走本地 socket 时遇到权限问题。

### Redis

```bash
sudo service redis-server start
redis-cli ping
```

### Kafka

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

## 构建运行

```bash
cmake -S . -B build-linux -DCMAKE_CXX_COMPILER=/usr/bin/g++
cmake --build build-linux

./build-linux/server config/config.yaml
```

点击事件 consumer：

```bash
python3 consumer/click_consumer.py
```

consumer 默认在 `127.0.0.1:9108/metrics` 暴露 Kafka lag 指标。

## 验证

```bash
curl http://127.0.0.1:9006/health

curl -X POST http://127.0.0.1:9006/api/shorten \
  -H 'Content-Type: application/json' \
  -d '{"long_url":"https://example.com"}'

curl -i http://127.0.0.1:9006/<short_code>

curl http://127.0.0.1:9006/metrics
curl http://127.0.0.1:9108/metrics
tail -f logs/access.jsonl
```

## 指标

核心 Prometheus 指标：

- `shorturl_http_requests_total`：QPS 用 `rate(...[1m])`
- `shorturl_http_request_duration_seconds_bucket`：P99 用 `histogram_quantile(0.99, rate(..._bucket[5m]))`
- `shorturl_cache_requests_total{result="hit|miss|filtered|unavailable"}`：缓存命中率
- `shorturl_kafka_publish_total{result="success|failure"}`：producer 投递情况
- `shorturl_kafka_consumer_lag`：consumer lag

更完整的可观测性说明见 [docs/phase5_observability.md](docs/phase5_observability.md)。

## 阶段文档

- [Phase 1 单机短链与基准压测](docs/phase1_baseline.md)
- [Phase 2 Redis 缓存一致性](docs/phase2_cache_consistency.md)
- [Phase 3 Kafka 可靠投递](docs/phase3_kafka_delivery.md)
- [Phase 4 分库分表水平扩展](docs/phase4_sharding.md)
- [Phase 5 可观测性](docs/phase5_observability.md)

## 致谢

原项目：[qinguoyi/TinyWebServer](https://github.com/qinguoyi/TinyWebServer)

参考：《Linux高性能服务器编程》，游双著。
