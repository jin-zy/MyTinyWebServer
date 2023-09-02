# MyTinyWebServer


## 简介
搭建 Linux 环境下 C++ 轻量级 Web 服务器。项目内容是对 Github 开源项目 [TinyWebServer]( https://github.com/qinguoyi/TinyWebServer) 的学习。原项目中同时实现了 epoll 的条件触发（LT）和边缘触发（ET），Reactor 事件处理模式和模拟 Proactor 事件处理模式，支持参数化自定义运行服务器，支持解析 GET 和 POST 请求。在学习过程中会先实现单模式下的服务器运行，再逐步添加不同模式和更多功能。

- 使用半同步半反应堆**线程池** + **非阻塞socket** + **epoll（ET）** + **模拟Proactor**事件处理模式的并发模型
- 使用**主从状态机**解析 HTTP 请求报文，当前暂仅支持 **GET** 请求获取静态资源
- 经 Webbench 压力测试可以实现**上万的并发连接**数据交换

## 运行

1. 构建并运行

```bash
$ make server
$ ./server
```

2. 浏览器端访问

```
http://<ip>:9190/
```

3. 个性化运行

```bash
$ ./server [-p port] [-t thread_number] [-c close_log]
```

- `-p`，自定义端口号，默认为 9190
- `-t`，自定义线程池中线程数量，默认为 8
- `-c`，选择关闭日志，默认打开
	- `0`，打开日志
	- `1`，关闭日志

```bash
# 示例
$ ./server -p 1004 -t 10 -c 1
```
- [x] 端口 1004
- [x] 线程池中线程数量 10
- [x] 关闭服务器日志

## 测试

1. 服务器测试环境

- CentOS Linux release 7.9.2009 (Core)	
- MySQL  Ver 14.14 Distrib 5.7.43
- 虚拟机配置：
	- 内存	4GB
	- 处理器	2\*4 
	
2. 浏览器测试环境

- Windows，Edge浏览器
- Linux，FireFoox浏览器 

3. webbench 压测结果

```bash
$ ./webbench -c 10500 -t 5 http://192.168.48.100:9190/
$ Webbench - Simple Web Benchmark 1.5
$ Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.

$ Benchmarking: GET http://192.168.48.100:9190/
$ 10500 clients, running 5 sec.

$ Speed=344724 pages/min, 764138 bytes/sec.
$ Requests: 28727 susceed, 0 failed.
```