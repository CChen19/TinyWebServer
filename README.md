
TinyWebServer
===============
基于 [qinguoyi/TinyWebServer](https://github.com/qinguoyi/TinyWebServer) 重构的 C++ 轻量级 RESTful Web 服务器。

保留原项目的 **线程池 + 非阻塞 socket + epoll + Reactor/Proactor** 并发模型，在此基础上引入现代化的路由框架，将 HTTP 协议层与业务逻辑彻底解耦。

Phase 0 重构变更
----------

### 新增：RESTful 路由框架

| 文件 | 说明 |
|------|------|
| `http/request.h` | 封装解析后的 HTTP 请求（method、path、query、headers、body、路径参数） |
| `http/response.{h,cpp}` | HTTP 响应构造器，支持 `set_json()` / `set_status()` / `serialize()` |
| `http/router.{h,cpp}` | URL→Handler 映射表，支持路径参数匹配（如 `/{code}`） |
| `handler/handler_base.h` | Handler 基础头文件 |
| `handler/health_handler.{h,cpp}` | Phase 0 验证：`GET /health` → `{"status":"ok","version":"0.1.0"}` |

### 新增：配置与依赖管理

| 文件 | 说明 |
|------|------|
| `config/config.{h,cpp}` | 基于 yaml-cpp 从 YAML 文件加载配置，取代命令行参数解析 |
| `config/config.yaml` | 统一配置文件（server / log / mysql 三段） |
| `CMakeLists.txt` | 替代原 makefile，`cmake --build build/` 一键构建 |
| `third_party/nlohmann/json.hpp` | nlohmann/json v3.11.3 single-header |

### 改造：HTTP 协议层瘦身

- **`http/http_conn.{h,cpp}`**：删除全部 CGI/mmap/文件服务/注册登录逻辑，流程改为 `parse → Router::dispatch → serialize response`
- **`webserver.{h,cpp}`**：`init()` 改为接收 `const Config&`，删除 `m_root` 和 `initmysql_result` 调用
- **`main.cpp`**：简化为加载 YAML → 注册路由 → 启动服务器
- **`CGImysql/sql_connection_pool.cpp`**：MySQL 不可用时优雅降级（不再 `exit(1)`）

### 删除的遗留

- `root/*.html`（9 个注册登录页面）
- `build.sh`（改用 CMake）
- `http_conn::initmysql_result()` 及全局 `users` map
- `do_request()` 中的 `'0'-'7'` 分支、CGI 标志、mmap 逻辑

### 保留不动

lock/、log/、timer/、threadpool/、CGImysql/sql_connection_pool.h、epoll 事件循环、Reactor/Proactor 切换

Phase 1 单机短链
----------

### 数据表

初始化脚本：

```bash
mysql -h127.0.0.1 -ushorturl -pshorturl shorturl < sql/001_short_url.sql
```

核心表：

```sql
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
```

### 短码生成

当前实现：单机紧凑 Snowflake ID → Base62。

- ID 结构：`29 bit timestamp_seconds_since_2026_01_01 + 12 bit sequence`
- 单机容量：约 17 年，每秒最多 4096 个短码
- Base62：`62^7 = 3,521,614,606,208`，7 位可表达约 3.5 万亿个编号
- MySQL 仍对 `short_code` 建唯一索引，极端碰撞时最多重试 3 次

三种生成方案 trade-off：

| 方案 | 优点 | 缺点 | 适用阶段 |
|------|------|------|----------|
| Snowflake + Base62 | 无中心化自增依赖；短码生成在应用内完成；天然按时间递增；后续可扩展 worker bit | 需要处理时钟回拨和序列耗尽；ID 结构要提前规划 | 当前 Phase 1，后续可平滑扩展 |
| MySQL 自增 + Base62 | 实现最简单；强一致；不需要额外组件 | 每次生成都依赖 DB insert/自增；数据库成为发号瓶颈；多库分片后迁移成本高 | Demo 或低 QPS 单库 |
| Redis INCR + Base62 | 性能高；实现简单；适合多实例共享计数器 | 引入 Redis 可用性和持久化问题；计数器恢复/主从切换要谨慎；仍是中心化发号 | 中期多实例但尚未分片 |

### 接口

创建短链：

```bash
curl -X POST http://localhost:9006/api/shorten \
  -H 'Content-Type: application/json' \
  -d '{"long_url":"https://example.com/a/very/long/url","expire_at":"2026-12-31 23:59:59"}'
```

跳转：

```bash
curl -i http://localhost:9006/abc1234
# HTTP/1.1 302 Found
# Location: https://example.com/a/very/long/url
```

Phase 2 Redis 缓存层
----------

Phase 2 在短链跳转链路上加入 Redis Cache-Aside 缓存：

- Redis client：`redis-plus-plus`
- 读路径：Bloom Filter → Redis → singleflight → MySQL → Redis
- 写路径：MySQL 成功后更新 Bloom Filter，并尝试写 Redis
- 穿透防护：启动时预热合法 `short_code` 集合到 Bloom Filter
- 击穿防护：热点 code 使用 per-code 互斥锁重建缓存
- 雪崩防护：Redis TTL 增加随机抖动
- 一致性：短链创建后几乎 immutable，Cache-Aside 足够；禁用/过期类写路径需要延时双删，进阶可用 Canal 订阅 binlog 异步刷新缓存

详细说明见 `docs/phase2_cache_consistency.md`。

Phase 3 Kafka 异步化
----------

Phase 3 把点击统计从跳转主链路异步化：

- C++ 服务接入 `librdkafka`
- `GET /{code}` 成功跳转时只投递点击事件到 Kafka，不同步写点击 MySQL
- Producer 可靠投递：`acks=all + enable.idempotence=true + retries`
- Broker 生产配置：`min.insync.replicas >= 2`，通常配合 `replication.factor >= 3`
- Consumer：独立 Python 进程，手动 commit，落库成功后提交 offset
- 业务幂等：`click_event.event_id` 主键去重
- 本地已用 Docker Kafka + librdkafka 跑通 producer → topic → consumer → MySQL 完整链路

详细说明见 `docs/phase3_kafka_delivery.md`。

项目结构
----------

```
TinyWebServer/
├── analytics/
│   └── click_event_producer.{h,cpp} # Kafka 点击事件 producer
├── consumer/
│   └── click_consumer.py      # 独立点击事件 consumer
├── lock/                # 线程同步机制封装
├── log/                 # 同步/异步日志系统
├── timer/               # 定时器处理非活动连接
├── CGImysql/            # 数据库连接池
├── threadpool/          # 半同步/半反应堆线程池
├── http/
│   ├── http_conn.{h,cpp}    # HTTP 协议层（读报文、写报文）
│   ├── router.{h,cpp}       # 路由表（URL → Handler）
│   ├── request.h             # 请求封装
│   └── response.{h,cpp}     # 响应构造
├── handler/
│   ├── handler_base.h        # Handler 基础定义
│   ├── health_handler.{h,cpp}# /health 端点
│   └── short_url_handler.{h,cpp}# 短链创建与跳转
├── shorturl/
│   ├── base62.{h,cpp}        # Base62 编码
│   ├── bloom_filter.{h,cpp}  # Bloom Filter 防穿透
│   ├── singleflight.{h,cpp}  # 热点 key 互斥重建
│   ├── short_url_cache.{h,cpp} # Redis Cache-Aside
│   ├── snowflake.{h,cpp}     # 单机紧凑 Snowflake 发号器
│   └── short_url_repository.{h,cpp} # MySQL 访问封装
├── sql/
│   ├── 001_short_url.sql     # short_url 建表脚本
│   └── 002_click_event.sql   # 点击事件表
├── docs/
│   ├── phase1_baseline.md    # Phase 1 压测记录
│   ├── phase2_cache_consistency.md # Phase 2 缓存一致性
│   └── phase3_kafka_delivery.md # Phase 3 可靠投递
├── config/
│   ├── config.{h,cpp}       # YAML 配置加载
│   └── config.yaml           # 配置文件
├── third_party/
│   └── nlohmann/json.hpp     # JSON 库
├── webserver.{h,cpp}         # 服务器核心（epoll 事件循环）
├── main.cpp                  # 入口
└── CMakeLists.txt
```

压力测试
----------

测试环境：WSL2 (Ubuntu 20.04)，GCC 9.4.0，MySQL 8.0.42

测试端点：`GET /health`（纯 JSON 响应，无磁盘 IO）

测试工具：webbench，持续 10 秒

| 并发数 | QPS (pages/min) | 每秒吞吐量 | 成功请求 | 失败 |
|--------|-----------------|-----------|---------|------|
| 500    | 287,922         | 585 KB/s  | 47,987  | 0    |
| 1,000  | 292,638         | 595 KB/s  | 48,773  | 0    |
| 5,000  | 309,660         | 630 KB/s  | 51,610  | 0    |

> 所有请求 0 失败。并发从 500→5000 时 QPS 稳步上升（/health 无 IO，瓶颈在 epoll 事件分发），此数据作为 Phase 1/2/3 架构演进的性能基线。

Phase 1 短链接口的压测记录统一维护在 `docs/phase1_baseline.md`。

快速运行
----------

### 依赖

```bash
# Ubuntu / Debian
sudo apt install -y build-essential cmake libmysqlclient-dev libyaml-cpp-dev mysql-server mysql-client redis-server libhiredis-dev librdkafka-dev python3-confluent-kafka python3-mysql.connector kafkacat
```

Redis 缓存层依赖 `redis-plus-plus` 和 `hiredis`。如果本机未安装 `redis-plus-plus`，CMake 会以 DB-only fallback 模式构建；安装依赖后重新配置即可启用 Redis。

`redis-plus-plus` 可从源码安装：

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

### MySQL 初始化

Ubuntu/WSL 下 `root@localhost` 常使用 `auth_socket`，项目不要用 root 账号连库。建议创建专用应用用户：

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
mysqladmin -h127.0.0.1 -ushorturl -pshorturl ping
```

所有项目命令都显式使用 `127.0.0.1`，避免 MySQL 客户端默认走 `/var/run/mysqld/mysqld.sock` 时被本机 socket 权限影响。

### Redis 启动

```bash
sudo service redis-server start
redis-cli ping
# PONG
```

### Kafka 启动

本地开发可用 Docker 启动单节点 Kafka；生产环境应使用多 broker，并按 Phase 3 文档设置 `replication.factor >= 3` 和 `min.insync.replicas >= 2`。

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

### 配置

编辑 `config/config.yaml`，按实际环境修改 MySQL 连接信息：

```yaml
server:
  port: 9006
  thread_num: 8

mysql:
  host: "127.0.0.1"
  port: 3306
  user: "shorturl"
  password: "shorturl"
  database: "shorturl"
  pool_size: 8

redis:
  enabled: true
  uri: "tcp://127.0.0.1:6379"

cache:
  ttl_seconds: 3600
  ttl_jitter_seconds: 300
  bloom_bits: 1048576
  bloom_hashes: 7

kafka:
  enabled: true
  brokers: "127.0.0.1:9092"
  click_topic: "shorturl.clicks"
  message_timeout_ms: 3000
  linger_ms: 5
  retries: 3
```

### 构建 & 运行

```bash
mkdir -p build && cd build
cmake ..
cmake --build .
cd ..
./build/server                    # 使用默认配置 config/config.yaml
./build/server /path/to/other.yaml  # 指定配置文件
```

启动点击事件 consumer：

```bash
python3 consumer/click_consumer.py
```

### 验证

```bash
curl http://localhost:9006/health
# {"status":"ok","version":"0.1.0"}

curl -X POST http://localhost:9006/api/shorten \
  -H 'Content-Type: application/json' \
  -d '{"long_url":"https://example.com"}'

curl -i http://localhost:9006/<short_code>
# HTTP/1.1 302 Found

redis-cli get shorturl:<short_code>
# https://example.com

mysql -h127.0.0.1 -ushorturl -pshorturl shorturl \
  -e "SELECT event_id, short_code, user_agent FROM click_event ORDER BY consumed_at DESC LIMIT 5;"

curl http://localhost:9006/notexist
# {"error":"not found","path":"/notexist"}
```

致谢
----------
原项目：[qinguoyi/TinyWebServer](https://github.com/qinguoyi/TinyWebServer)

Linux高性能服务器编程，游双著.
