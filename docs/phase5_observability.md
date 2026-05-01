# Phase 5 Observability

Phase 5 目标：让短链系统具备可解释、可压测、可排障的观测能力。实现分两块：

- C++ server 暴露 `/metrics`，输出 HTTP、延迟、缓存、Kafka producer 指标。
- Python click consumer 暴露 `/metrics`，输出消费结果和 Kafka lag。
- C++ server 写 JSONL 结构化访问日志到 `logs/access.jsonl`。

## Prometheus 指标

### QPS

指标：

```text
shorturl_http_requests_total{method,route,status_class}
```

PromQL：

```promql
sum(rate(shorturl_http_requests_total[1m])) by (route)
```

路由标签使用低基数 route pattern，例如 `/api/shorten`、`/{code}`、`/metrics`，不会把真实短码写进 label。

### P99 延迟

指标：

```text
shorturl_http_request_duration_seconds_bucket
shorturl_http_request_duration_seconds_sum
shorturl_http_request_duration_seconds_count
```

PromQL：

```promql
histogram_quantile(
  0.99,
  sum(rate(shorturl_http_request_duration_seconds_bucket[5m])) by (le)
)
```

当前是全局 HTTP latency histogram。生产里可以按 `route` 拆分 histogram，但要注意标签基数。

### 缓存命中率

指标：

```text
shorturl_cache_requests_total{result="hit"}
shorturl_cache_requests_total{result="miss"}
shorturl_cache_requests_total{result="filtered"}
shorturl_cache_requests_total{result="unavailable"}
```

PromQL：

```promql
sum(rate(shorturl_cache_requests_total{result="hit"}[5m]))
/
sum(rate(shorturl_cache_requests_total{result=~"hit|miss"}[5m]))
```

`filtered` 表示 Bloom Filter 拦截，属于穿透防护效果；`unavailable` 表示 Redis 不可用或未启用。

### Kafka Producer

指标：

```text
shorturl_kafka_publish_total{result="success"}
shorturl_kafka_publish_total{result="failure"}
```

这能说明 C++ 跳转链路是否成功把点击事件提交给本地 producer。可靠投递仍依赖 Phase 3 的 producer 配置：

```text
acks=all
enable.idempotence=true
retries=N
```

### Kafka Lag

Kafka lag 由独立 consumer 暴露：

```text
shorturl_kafka_consumer_lag
shorturl_click_consumer_events_total{result="consumed|inserted|invalid|failed"}
shorturl_click_consumer_last_commit_timestamp_seconds
```

启动：

```bash
METRICS_PORT=9108 python3 consumer/click_consumer.py
curl http://127.0.0.1:9108/metrics
```

`shorturl_kafka_consumer_lag` 通过 consumer 当前 assignment 的 high watermark 和 position 计算，表示已分配分区上还没有处理的消息数。

## 结构化日志

配置：

```yaml
observability:
  structured_log_enabled: true
  structured_log_path: "./logs/access.jsonl"
```

示例：

```json
{"ts":"2026-05-01T09:30:00.123","event":"http_access","method":"GET","path":"/Kv9o2Y","route":"/{code}","status":302,"duration_ms":1.420,"remote_addr":"127.0.0.1","user_agent":"curl/7.68.0"}
```

访问日志是 JSON Lines，后续可以直接接入 Loki、Filebeat、Vector 或本地 `jq` 分析：

```bash
tail -f logs/access.jsonl | jq .
```

## 告警建议

| 目标 | PromQL 示例 |
|------|-------------|
| 5xx 错误率 | `sum(rate(shorturl_http_requests_total{status_class="5xx"}[5m])) / sum(rate(shorturl_http_requests_total[5m]))` |
| P99 延迟 | `histogram_quantile(0.99, sum(rate(shorturl_http_request_duration_seconds_bucket[5m])) by (le))` |
| Redis 命中率 | `sum(rate(shorturl_cache_requests_total{result="hit"}[5m])) / sum(rate(shorturl_cache_requests_total{result=~"hit|miss"}[5m]))` |
| Kafka 投递失败 | `sum(rate(shorturl_kafka_publish_total{result="failure"}[5m]))` |
| Kafka 堆积 | `shorturl_kafka_consumer_lag` |

## 本地验证记录

服务端指标：

```bash
curl http://127.0.0.1:9006/metrics
```

关键输出：

```text
shorturl_http_requests_total{method="GET",route="/health",status_class="2xx"} 1
shorturl_http_requests_total{method="GET",route="/{code}",status_class="3xx"} 1
shorturl_cache_requests_total{result="hit"} 1
shorturl_kafka_publish_total{result="success"} 1
```

consumer 指标：

```bash
curl http://127.0.0.1:9108/metrics
```

关键输出：

```text
shorturl_kafka_consumer_lag 0
shorturl_click_consumer_events_total{result="inserted"} 1
```

结构化日志：

```bash
tail -n 1 logs/access.jsonl
```

关键字段：

```json
{"event":"http_access","route":"/{code}","status":302}
```
