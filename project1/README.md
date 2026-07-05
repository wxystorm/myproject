

```markdown
# 🤖 C-Based LLM Client & Coding Agent Core

本项目是一个基于 C 语言开发的轻量级远程 LLM (大语言模型) 客户端。项目放弃了高层网络库的封装，完全基于底层 Linux Sockets 和 TCP 字节流构建，实现了与 OpenAI/Qwen 兼容接口的 HTTP 协议通信、JSON 序列化组包以及稳健的响应解析[cite: 2]。该模块未来将作为 AI 编码智能体（Coding Agent）的核心通信层[cite: 2]。

---

## ✨ 核心特性 (Features)

* **纯 C 语言原生 Socket 通信**：基于 `socket()`、`connect()` 等底层系统调用建立稳健的 TCP 连接。
* **协议无关的地址解析**：采用现代化的 `getaddrinfo()` 模式实现主机名与端口的自动解析，兼容未来网络扩展。
* **原生 HTTP/1.1 协议动态组包**：纯手动构建符合规范的 HTTP POST 请求体，精确控制 `\r\n` 换行边界与 `Content-Length`。
* **流式响应缓冲区管理**：设计了能够应对 TCP 字节流高度碎片化（Fragmentation）的鲁棒接收机制，支持跨多次 `recv()` 调用的边界拼接。
* **内存安全保障**：全面采用安全字符串函数 `snprintf()` 防止缓冲区溢出[cite: 2]，并完美通过 AddressSanitizer (ASan) 内存漏洞检测。
* **灵活的环境变量配置**：支持通过外部环境变量动态切换模型、API 密钥以及目标服务器，无需重新编译代码。

---

## 🛠️ 环境依赖与配置 (Prerequisites)

在开始运行前，请确保你的开发环境（Linux / WSL2）已安装 GCC、Make 以及 Caddy[cite: 2]。

### 1. 配置反向代理 (Caddy)
由于本实验专注于底层 Socket 与 HTTP 协议解析，为了避免引入复杂的 TLS/HTTPS 握手，我们需要在本地运行一个 Caddy 反向代理[cite: 2]。它负责接收我们 C 程序的明文 HTTP 请求，并将其安全地转发至交大内部的 HTTPS 远程大模型接口[cite: 2]：

```bash
# 启动本地反向代理，将本地 18080 端口映射到远程大模型服务[cite: 2]
caddy reverse-proxy --from :18080 --to [https://models.sjtu.edu.cn](https://models.sjtu.edu.cn)

```

### 2. 模拟服务器 (Mock Server)

如果在离线开发或自动化测试阶段需要确定性的、可复现的响应，可以运行本地的 Python 模拟服务器：

```bash
python3 tools/mock_server.py

```

---

## 📦 快速上手 (Quick Start)

### 1. 编译项目

项目配置了完善的 `Makefile`。直接在根目录下运行以下命令进行编译：

```bash
make

```

如果需要启用 AddressSanitizer 辅助排查内存越界或悬空指针等 Bug，请运行：

```bash
make asan

```

### 2. 配置环境变量

在使用客户端前，需要设置你的 API 密钥。程序会自动读取这些变量作为默认配置：

```bash
export API_KEY="AI平台API密钥"  
export LLM_HOST="127.0.0.1"          
export LLM_PORT="18080"              
export MODEL="模型名称"            

```

### 3. 运行交互

编译完成后，你可以通过标准输入（stdin）向程序输入 Prompt 提示词：

```bash
echo "请问什么是 TCP 字节流？" | ./llm

```

### 4. 运行自动化测试

运行以下命令可以调用自动化测试脚本对网络、状态码和异常边界进行全方位评测：

```bash
make test        # 运行所有测试阶段
make test-asan   # 在 ASan 监控下跑测试

```

---

## 📂 项目文件结构 (Project Structure)

```text
.
├── context/            # 智能体上下文/多轮对话历史状态管理
├── libs/               # 项目自定义公共库与辅助工具
├── ui/                 # 终端交互界面或格式化输出模块
├── tools/              # 包含自动化评测及 mock_server.py 等辅助脚本
├── tests/              # 自动化单元测试集合
├── Makefile            # 自动化编译与测试脚本
├── main.c              # 程序主入口，串联完整的请求响应生命周期
├── http.c / http.h     # HTTP 请求组装、Socket 连接及字节流稳健接收的实现核心
├── message.c / h       # 大模型消息体（Messages）的结构定义与 JSON 格式化封装
├── config.c / h        # 环境配置与环境变量读取解析
└── util.c / h          # 基础字符串处理及错误报告辅助函数

```



