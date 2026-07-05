# 🚀 项目名称 (e.g., My Awesome Network Server)

[![GitHub license](https://img.shields.io/github/license/mashape/apistatus.svg)](🪪开源协议链接)]

> **一句话简介**：这是一个基于c语言开发的一个ai_agent课程项目，通过远程调用api在终端实现与大模型的交互，包括询问，分析文件、文件夹的架构，以及修改创建文件。内置沙箱检查，
> 当要修改工作区外的目录会触发拦截，保障文件的安全。同时支持offload，避免占用过多的上下文，支持对话保存，在下一次对话开始时会自动加载对话内容。

---

## 🛠️ 环境依赖 (Prerequisites)

在编译和运行本项目之前，请确保你的开发环境满足以下要求：

- **操作系统**：Linux (Ubuntu 20.04+) / macOS / Windows (WSL2)
- **编译器**：GCC 9.0+ 或 Clang 11.0+
- **构建工具**：Make / CMake

---

## 📦 快速上手 (Quick Start)

### 1. 克隆项目到本地
```bash
git clone https://github.com/wxystorm/myproject2.git
cd project2
```
### 2.编译
```bash
make
```
### 3.配置api
```bash
export API_KEY="你的api"
export LLM_HOST="127.0.0.1"  
export LLM_PORT="端口"
```
注意该项目暂不支持https协议，所以需要再本地使用caddy进行反代
例
```bash
caddy reverse-proxy --from :18080 --to 目标网站
```
### 4.启动
```bash
./agent
```
