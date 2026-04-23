
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

项目结构
----------

```
TinyWebServer/
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
│   └── health_handler.{h,cpp}# /health 端点
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

快速运行
----------

### 依赖

```bash
# Ubuntu / Debian
sudo apt install -y build-essential cmake libmysqlclient-dev libyaml-cpp-dev
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
  user: "root"
  password: "root"
  database: "shorturl"
  pool_size: 8
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

### 验证

```bash
curl http://localhost:9006/health
# {"status":"ok","version":"0.1.0"}

curl http://localhost:9006/notexist
# {"error":"not found","path":"/notexist"}
```

致谢
----------
原项目：[qinguoyi/TinyWebServer](https://github.com/qinguoyi/TinyWebServer)

Linux高性能服务器编程，游双著.
