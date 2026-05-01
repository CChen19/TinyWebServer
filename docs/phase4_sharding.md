# Phase 4 Sharding

Phase 4 目标：为短链核心表加入水平扩展能力。C++ 没有 Sharding-JDBC 这类现成透明代理，本项目实现一个薄路由层，把 `short_code` 映射到具体的 `database.table`。

## 分片模型

默认配置：

```yaml
sharding:
  enabled: true
  database_prefix: "shorturl_"
  table_prefix: "short_url_"
  database_count: 4
  table_count: 4
```

物理布局：

```text
4 databases x 4 tables = 16 physical tables

shorturl_00.short_url_00 ... shorturl_00.short_url_03
shorturl_01.short_url_00 ... shorturl_01.short_url_03
shorturl_02.short_url_00 ... shorturl_02.short_url_03
shorturl_03.short_url_00 ... shorturl_03.short_url_03
```

初始化：

```bash
sudo mysql < sql/003_sharded_short_url.sql
```

该脚本会创建 4 个库、16 张表，并授权给应用用户 `shorturl`。

## 分片键

分片键选择 `short_code`，原因：

- 读路径 `GET /{code}` 天然携带 `short_code`。
- 读请求可以直接定位一个物理表，避免广播查询。
- 短链系统通常读多写少，读路径友好比写路径批量均衡更重要。

路由算法：

```text
hash = fnv1a_64(short_code)
bucket = hash % (database_count * table_count)
database_index = bucket / table_count
table_index = bucket % table_count
```

示例：

```text
KuVw8U -> hash -> bucket 7 -> shorturl_01.short_url_03
```

代码落点：

| 模块 | 说明 |
|------|------|
| `shorturl/shard_router.{h,cpp}` | 稳定 hash + 分片路由 |
| `shorturl/short_url_repository.cpp` | insert/select/list_active_codes 使用路由结果 |
| `config/config.yaml` | 分片开关、库前缀、表前缀、库数、表数 |

## 路由逻辑

写入：

```text
Snowflake ID -> Base62 short_code
  -> ShardRouter.route_for_code(short_code)
  -> INSERT INTO shorturl_xx.short_url_yy
```

读取：

```text
GET /{code}
  -> Bloom Filter
  -> Redis
  -> ShardRouter.route_for_code(code)
  -> SELECT FROM shorturl_xx.short_url_yy
```

Bloom Filter 预热：

```text
scan all 4 x N physical tables
  -> load active short_code into BF
```

预热是后台/启动阶段动作，可以接受扫全分片；线上可改成定时任务或把 BF 持久化到 Redis/本地文件。

## 取模 vs 一致性 Hash

### 当前选择：固定取模

优点：

- 实现简单。
- 路由结果稳定、可解释。
- 对固定规模 `4 x N` 的面试项目很合适。
- 无需维护虚拟节点环和元数据中心。

缺点：

- 扩容时如果直接从 4 库改成 8 库，大量 key 会重映射。
- 需要迁移数据或做新老路由双读。

### 一致性 Hash

优点：

- 增减节点时迁移比例较低。
- 更适合节点规模变化频繁的缓存或存储集群。

缺点：

- 需要维护 hash ring、虚拟节点和节点元数据。
- MySQL 分库分表里仍要处理物理表迁移、双写、回填、校验。
- 对短链这种可提前规划容量的业务，复杂度不一定划算。

面试回答：当前阶段用固定取模，因为容量规划明确、实现可控；如果进入动态扩容阶段，可以引入一致性 hash 或基于 range/hash 的路由元数据表。

## 扩容迁移方案

推荐不要直接修改 `database_count` 让所有老数据重映射。更稳的做法：

1. 引入路由版本：`route_version = v1/v2`。
2. 新写入走 v2 新分片规则。
3. 老数据仍按 v1 读。
4. 后台迁移任务按批次搬迁老数据。
5. 迁移时写入映射表或标记迁移进度。
6. 读路径在迁移窗口支持双读：先 v2，miss 再 v1。
7. 校验完成后切换为只读 v2，最终下线 v1。

迁移期间需要保证：

- `short_code` 全局唯一。
- Redis key 不依赖物理分片，因此迁移对缓存透明。
- Kafka 点击事件只带 `short_code`，consumer 如需查短链元数据，也必须通过同一套路由层。

## 如何避免跨分片查询

短链核心接口必须围绕 `short_code` 设计：

- `GET /{code}`：天然单分片。
- `POST /api/shorten`：生成 code 后单分片写入。
- `click_event`：按事件流异步落库，不反查所有短链分片。

不建议在在线接口做：

- 按 `long_url` 模糊查询短链。
- 按创建时间跨全库分页。
- 全局统计实时扫分片。

这些需求应走离线数仓、搜索索引、异步宽表或单独的聚合表。

## 长链去重 Trade-off

如果产品要求“同一长链只生成一个短码”，需要反向索引：

```text
long_url_hash -> short_code
```

难点：

- `long_url` 不是当前分片键，按 `short_code` 分片后无法单分片判断长链是否已存在。
- 如果把反向索引集中放一张表，它会成为写入热点和单点瓶颈。
- 如果反向索引也分片，需要选择 `long_url_hash` 作为另一个分片键，短链主表和反向索引就成了双写分布式一致性问题。

可选方案：

| 方案 | 优点 | 缺点 |
|------|------|------|
| 不去重 | 写路径最简单，完全按 `short_code` 路由 | 同一长链可能有多个短码 |
| 集中反向索引表 | 查询简单，唯一约束好做 | 单点瓶颈，扩容困难 |
| 反向索引分片 | 可水平扩展 | 双写、回滚、幂等和一致性更复杂 |
| Redis/布隆辅助去重 | 快，成本低 | 不能作为强一致唯一约束 |

面试回答：当前阶段不做强去重，保持短链创建路径简单可扩展；如果必须去重，用 `long_url_hash` 建反向索引，并把它当作另一套分片系统设计，配合唯一约束、幂等 token 和失败补偿。

## 本地验证记录

环境：

- MySQL 8.0.42
- Redis 127.0.0.1:6379
- Docker Kafka `apache/kafka:3.7.0`
- 服务端口 `9006`

初始化校验：

```bash
mysql -h127.0.0.1 -ushorturl -pshorturl -N \
  -e "SHOW DATABASES LIKE 'shorturl_%';"

mysql -h127.0.0.1 -ushorturl -pshorturl -N \
  -e "SELECT COUNT(*) FROM information_schema.tables \
      WHERE table_schema LIKE 'shorturl\\_%' \
      AND table_name LIKE 'short_url\\_%';"
```

结果：

```text
shorturl_00
shorturl_01
shorturl_02
shorturl_03

16
```

HTTP 链路：

```bash
curl -i -X POST http://127.0.0.1:9006/api/shorten \
  -H 'Content-Type: application/json' \
  -d '{"long_url":"https://example.com/phase4-sharding-real-e2e"}'

curl -i http://127.0.0.1:9006/Kv9o2Y \
  -A 'phase4-sharding-e2e-restart'
```

结果：

```text
POST /api/shorten -> 201 Created
short_code=Kv9o2Y

GET /Kv9o2Y -> 302 Found
Location: https://example.com/phase4-sharding-real-e2e
```

分片落点：

```text
shorturl_02.short_url_03  Kv9o2Y  https://example.com/phase4-sharding-real-e2e
```

旧单表校验：

```text
SELECT COUNT(*) FROM shorturl.short_url WHERE short_code='Kv9o2Y';
0
```

Kafka 点击事件也已在 `shorturl.clicks` topic 中观察到：

```json
{"short_code":"Kv9o2Y","user_agent":"phase4-sharding-e2e-restart"}
```
