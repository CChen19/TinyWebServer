# Phase 3 Kafka Async Delivery

Phase 3 目标：把点击统计从跳转主链路中异步化。用户访问 `GET /{code}` 时，主链路只做缓存/DB 查询、302 跳转和 Kafka 点击事件投递，不同步写点击 MySQL。

## 依赖

C++ 服务使用 `librdkafka` producer。CMake 会自动探测：

- `librdkafka/rdkafka.h`
- `librdkafka`

如果依赖缺失，项目仍可构建，Kafka producer 以 no-op fallback 运行。

Ubuntu 安装：

```bash
sudo apt install -y librdkafka-dev
```

本地 Kafka broker 可用 Docker 启动单节点 KRaft：

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
```

Python consumer 依赖：

```bash
pip install confluent-kafka mysql-connector-python
```

## 配置

```yaml
kafka:
  enabled: true
  brokers: "127.0.0.1:9092"
  click_topic: "shorturl.clicks"
  message_timeout_ms: 3000
  linger_ms: 5
  retries: 3
```

点击事件 topic：

```text
shorturl.clicks
```

消息 key：

```text
short_code
```

消息 value：

```json
{
  "event_id": "KuC1f0",
  "short_code": "KuBmfe",
  "clicked_at_ms": 1777624430123,
  "user_agent": "curl/7.68.0",
  "referer": "",
  "x_forwarded_for": ""
}
```

## 主链路变化

Phase 2：

```text
GET /{code}
  -> Bloom Filter
  -> Redis
  -> MySQL fallback
  -> Redis rebuild
  -> 302
```

Phase 3：

```text
GET /{code}
  -> Bloom Filter
  -> Redis
  -> MySQL fallback
  -> Redis rebuild
  -> Kafka produce click event
  -> 302
```

点击统计不在主链路写 MySQL。点击事件进入 Kafka 后，由独立 consumer 异步落库。

## 可靠投递三组配置

### Producer

当前 C++ producer 固定设置：

```text
acks=all
enable.idempotence=true
retries={config.kafka.retries}
```

含义：

- `acks=all`：leader 等待 ISR 中副本确认后再 ack。
- `enable.idempotence=true`：生产者重试时 Broker 可基于 Producer ID/Sequence 去重，避免重试造成重复写入。
- `retries`：网络抖动、leader 切换等临时错误自动重试。

代码里 `rd_kafka_producev` 只把消息交给 librdkafka 本地队列，实际网络发送和重试由 librdkafka 后台线程处理；请求线程不等待 consumer 落库，因此点击统计不会重新拉长 302 主链路。

面试图示：

```text
WebServer Producer
  acks=all
  idempotence=true
  retries=N
      |
      v
Kafka Leader ---- replicate ---- ISR Followers
      |
      v
ack after ISR success
```

### Broker

生产建议：

```text
replication.factor >= 3
min.insync.replicas >= 2
```

配合 producer `acks=all` 后，只要至少 2 个 ISR 副本确认，producer 才收到成功响应。这样单个 broker 宕机不会让已 ack 的消息丢失。

本地单 broker 开发环境无法设置 `min.insync.replicas >= 2`，可以临时用 1；但面试和生产方案必须讲清楚 2+ ISR。

### Consumer

独立 consumer 使用：

```text
enable.auto.commit=false
manual commit after DB success
business idempotence by event_id
```

流程：

```text
poll message
  -> parse event
  -> INSERT click_event(event_id, ...)
  -> ON DUPLICATE KEY no-op
  -> commit Kafka offset
```

如果 consumer 在写 DB 后、commit offset 前崩溃，消息会被重复消费；`event_id` 主键保证业务层幂等。

## Consumer 落库

点击事件表：

```sql
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
```

初始化：

```bash
mysql -h127.0.0.1 -ushorturl -pshorturl shorturl < sql/002_click_event.sql
```

运行 consumer：

```bash
python3 consumer/click_consumer.py
```

环境变量：

```bash
KAFKA_BROKERS=127.0.0.1:9092
KAFKA_CLICK_TOPIC=shorturl.clicks
KAFKA_GROUP_ID=shorturl-click-writer
MYSQL_HOST=127.0.0.1
MYSQL_USER=shorturl
MYSQL_PASSWORD=shorturl
MYSQL_DATABASE=shorturl
```

## 当前代码落点

| 模块 | 说明 |
|------|------|
| `analytics/click_event_producer.{h,cpp}` | C++ Kafka producer，可靠投递配置 |
| `handler/short_url_handler.cpp` | 302 前投递点击事件 |
| `consumer/click_consumer.py` | 独立 consumer，手动 commit + event_id 幂等 |
| `sql/002_click_event.sql` | 点击事件落库表 |
| `config/config.yaml` | Kafka broker/topic/producer 参数 |

## 验证命令

安装依赖后重新 CMake，warning 应消失：

```bash
cmake -S . -B build-linux -DCMAKE_CXX_COMPILER=/usr/bin/g++
cmake --build build-linux
ldd build-linux/server | grep rdkafka
# librdkafka.so.1 => /lib/x86_64-linux-gnu/librdkafka.so.1
```

创建 topic 示例：

```bash
kafka-topics.sh --bootstrap-server 127.0.0.1:9092 \
  --create --if-not-exists \
  --topic shorturl.clicks \
  --partitions 3 \
  --replication-factor 1
```

生产环境 topic 应使用 replication factor 3，并设置：

```bash
kafka-configs.sh --bootstrap-server <brokers> \
  --alter --entity-type topics --entity-name shorturl.clicks \
  --add-config min.insync.replicas=2
```

## 真实链路验证记录

本地依赖：

```text
Docker Kafka: apache/kafka:3.7.0
librdkafka: 1.2.1
Redis: PONG
MySQL: shorturl.short_url + shorturl.click_event
```

C++ 服务已确认链接真实 Kafka client：

```text
librdkafka.so.1 => /lib/x86_64-linux-gnu/librdkafka.so.1
```

topic：

```text
Topic: shorturl.clicks
PartitionCount: 3
ReplicationFactor: 1
```

创建短链：

```bash
curl -sS -X POST http://127.0.0.1:9006/api/shorten \
  -H 'Content-Type: application/json' \
  -d '{"long_url":"https://example.com/phase3-kafka-real-2"}'
```

响应：

```json
{
  "expire_at": null,
  "long_url": "https://example.com/phase3-kafka-real-2",
  "short_code": "KuVw8U",
  "short_url": "http://127.0.0.1:9006/KuVw8U"
}
```

触发点击：

```bash
curl -i -H 'User-Agent: phase3-e2e-test-2' http://127.0.0.1:9006/KuVw8U
# HTTP/1.1 302 Found
# Location: https://example.com/phase3-kafka-real-2
```

Kafka topic 中的事件：

```json
{
  "clicked_at_ms": 1777625785794,
  "event_id": "KuVIVG",
  "referer": "",
  "short_code": "KuVw8U",
  "user_agent": "phase3-e2e-test-2",
  "x_forwarded_for": ""
}
```

consumer 落库：

```sql
SELECT event_id, short_code, user_agent FROM click_event;
```

结果：

```text
event_id  short_code  user_agent
KuVIVG    KuVw8U      phase3-e2e-test-2
```

consumer group lag：

```text
shorturl-click-writer-test-2 shorturl.clicks partition 0 lag 0
shorturl-click-writer-test-2 shorturl.clicks partition 2 lag 0
```

这次验证覆盖了完整链路：

```text
GET /{code}
  -> 302
  -> C++ librdkafka producer
  -> Kafka topic shorturl.clicks
  -> Python consumer manual commit
  -> MySQL click_event
```
