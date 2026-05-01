# Phase 2 Redis Cache Consistency

Phase 2 目标：为短链跳转链路加入 Redis 缓存层，并把缓存一致性、穿透、击穿、雪崩作为核心章节沉淀下来。

## 依赖

项目通过 `redis-plus-plus` 接入 Redis。CMake 会自动探测：

- `sw/redis++/redis++.h`
- `libredis++`
- `libhiredis`

如果依赖缺失，项目仍可构建，缓存层以 DB-only fallback 运行；安装依赖后重新 CMake 配置即可启用真实 Redis。

本地安装示例：

```bash
sudo apt install -y redis-server libhiredis-dev

cd /tmp
git clone https://github.com/sewenew/redis-plus-plus.git
cd redis-plus-plus
mkdir -p build && cd build
cmake .. -DREDIS_PLUS_PLUS_CXX_STANDARD=14
make -j
sudo make install
sudo ldconfig
```

## 配置

```yaml
redis:
  enabled: true
  uri: "tcp://127.0.0.1:6379"
  connect_timeout_ms: 200
  socket_timeout_ms: 200

cache:
  ttl_seconds: 3600
  ttl_jitter_seconds: 300
  bloom_bits: 1048576
  bloom_hashes: 7
```

Redis key：

```text
shorturl:{code} -> long_url
```

## MySQL 连接约定

项目使用专用应用用户：

```yaml
mysql:
  host: "127.0.0.1"
  user: "shorturl"
  password: "shorturl"
  database: "shorturl"
```

显式使用 `127.0.0.1` 是为了走 TCP。Ubuntu/WSL 环境里，MySQL 客户端如果不写 `-h127.0.0.1`，常会默认走 Unix socket：

```text
/var/run/mysqld/mysqld.sock
```

当 socket 目录权限异常或 `root@localhost` 使用 `auth_socket` 时，会出现 `ERROR 2002` 或 `ERROR 1698`。项目运行不依赖 socket，只要 TCP 验证通过即可：

```bash
mysqladmin -h127.0.0.1 -ushorturl -pshorturl ping
mysql -h127.0.0.1 -ushorturl -pshorturl shorturl -e "SHOW TABLES;"
```

## Cache-Aside 路径

### 读路径：GET /{code}

1. Bloom Filter 判断 `code` 是否可能存在。
2. BF 判定不存在：直接返回 404，拦截缓存穿透。
3. BF 判定可能存在：读 Redis。
4. Redis 命中：返回 302。
5. Redis miss：获取该 `code` 对应的 singleflight mutex。
6. 加锁后 double-check Redis，避免等待期间其他线程已重建。
7. 仍 miss：查询 MySQL。
8. MySQL 命中且无 `expire_at`：写 Redis，TTL 加随机抖动，然后返回 302。
9. MySQL 命中但有 `expire_at`：只补充 Bloom Filter，不写 Redis，避免业务过期后缓存继续 302。
10. MySQL 已过期：删除 Redis，返回 410。
11. MySQL 不存在：返回 404。

### 写路径：POST /api/shorten

1. 生成 Snowflake ID。
2. Base62 得到 `short_code`。
3. 写 MySQL。
4. DB 成功后把 `short_code` 加入 Bloom Filter。
5. 如果短链无 `expire_at`，尝试写入 Redis：`SETEX shorturl:{code} ttl+jitter long_url`。
6. 如果短链有 `expire_at`，只写 Bloom Filter，不写 Redis，优先保证过期语义正确。

短链创建后几乎 immutable，所以永久短链创建成功后预热缓存是可以接受的；即使 Redis 写失败，也不影响主路径，后续读请求会回源 MySQL 并重建缓存。

## 三防

### 穿透：Bloom Filter

短链 `code` 集合天然适合 BF：

- 合法 code 集合相对固定。
- code 创建后基本不删除。
- BF 有假阳性但无假阴性，适合在 DB 前做快速拦截。

启动时从 MySQL 预热所有未过期 code：

```sql
SELECT short_code FROM short_url
WHERE expire_at IS NULL OR expire_at > NOW();
```

新短链创建成功后实时 `BF.add(code)`。如果 BF 未预热成功，为了可用性，系统仍然可以走 DB-only 路径；但穿透防护效果会下降。

### 击穿：Singleflight

热点 code 过期或 Redis miss 时，多个线程可能同时回源 DB。当前实现按 code 获取互斥锁：

```text
singleflight key = short_code
```

同一个 code 同一时间只允许一个线程重建缓存。其他线程等待锁，拿到锁后先 double-check Redis，命中则直接返回。

### 雪崩：TTL 随机抖动

Redis 写入时使用：

```text
ttl = ttl_seconds + random(0, ttl_jitter_seconds)
```

这样批量写入或集中预热的 key 不会在同一秒集中过期，降低大面积回源 MySQL 的风险。

## 一致性策略

### 当前阶段：Cache-Aside 足够

短链核心语义是“创建后几乎不可变”：

- `short_code -> long_url` 创建后不更新。
- 读多写少。
- 创建接口以 MySQL 写成功为准。
- Redis 只是加速层，miss 可回源。

因此 Phase 2 使用 Cache-Aside 就足够：写 DB 后更新 BF 并尝试写缓存；读 miss 时回源 DB 再重建缓存。

### 禁用/过期场景：延时双删

如果后续加入禁用短链、修改过期时间、人工封禁等写路径，需要讨论延时双删：

1. 先删除 Redis。
2. 更新 MySQL。
3. 延迟几十到几百毫秒后再次删除 Redis。

原因：并发读可能在第一次删除后、DB 更新前读到旧值并重建缓存；第二次删除用于清掉这个窗口期产生的脏缓存。

对于短链禁用，推荐 DB 增加 `disabled_at` 或 `status` 字段，并在查询条件里统一过滤。

### Bonus：Canal 订阅 binlog

如果面试继续追问更强一致性或多服务写入：

- MySQL binlog 作为变更源。
- Canal 订阅 `short_url` 表变更。
- 消费变更事件异步删除或刷新 Redis。
- 与延时双删相比，Canal 更适合多写入口、多服务、多语言系统。

这个方案本质是用 CDC 把 DB 变更广播给缓存维护组件，降低业务代码里到处写删缓存逻辑的耦合。

## 当前代码落点

| 模块 | 说明 |
|------|------|
| `shorturl/short_url_cache.{h,cpp}` | Redis Cache-Aside、TTL jitter、缓存降级 |
| `shorturl/bloom_filter.{h,cpp}` | 本地 Bloom Filter |
| `shorturl/singleflight.{h,cpp}` | 按 code 的互斥重建 |
| `shorturl/short_url_repository.{h,cpp}` | 增加合法 code 预热查询 |
| `handler/short_url_handler.cpp` | 接入读写缓存路径 |

## 验证记录

本地端到端验证结果：

```bash
redis-cli ping
# PONG

mysqladmin -h127.0.0.1 -ushorturl -pshorturl ping
# mysqld is alive

mysql -h127.0.0.1 -ushorturl -pshorturl shorturl -e "SHOW TABLES;"
# short_url
```

创建短链：

```bash
curl -sS -X POST http://127.0.0.1:9006/api/shorten \
  -H 'Content-Type: application/json' \
  -d '{"long_url":"https://example.com/phase2-test"}'
```

示例响应：

```json
{
  "expire_at": null,
  "long_url": "https://example.com/phase2-test",
  "short_code": "KuBmfe",
  "short_url": "http://127.0.0.1:9006/KuBmfe"
}
```

缓存验证：

```bash
redis-cli get shorturl:KuBmfe
# https://example.com/phase2-test

redis-cli ttl shorturl:KuBmfe
# 3806
```

`3806 = 3600 + jitter`，说明 TTL 随机抖动已生效。

跳转验证：

```bash
curl -i http://127.0.0.1:9006/KuBmfe
# HTTP/1.1 302 Found
# Location: https://example.com/phase2-test
```
