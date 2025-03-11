# MyTinyWebServer

## 简介

搭建 Linux 环境下 C++ 轻量级 Web 服务器。项目内容是对开源项目 [TinyWebServer]( https://github.com/qinguoyi/TinyWebServer) 的研读学习。

- 使用**线程池** + **非阻塞socket** + **epoll（ET）** + **模拟 Proactor** 事件处理模式的并发模型；
- 使用**主从状态机**解析 HTTP 请求报文，支持 **GET** 和 **POST** 请求服务器资源；
- 使用 MySQL 数据库实现用户**注册**、**登录**功能，可请求服务器的图片和视频文件；
- 使用**单例模式**+**阻塞队列**实现**异步日志系统**，记录服务器的运行状态；
- 经 Webbench 压力测试可以实现**上万的并发连接**数据交换。

## 运行

### 数据库配置

1. 服务器运行前确认系统已安装 MySQL 数据库，并正确运行

```bash
# 安装 mysql 服务器
sudo apt install mysql-server

# 检查运行状态
systemctl status mysql

sudo systemctl start mysql		# 启动
sudo systemctl stop mysql		# 停止
sudo systemctl restart mysql	# 重启
```

2. 创建相关数据库和表

```sql
# 创建 webserv 数据库
CREATE DATABASE webserv;

# 创建 user 表
USE webserv;
CREATE TABLE user(
	username char(50) NULL,
	password char(50) NULL
) ENGINE=InnoDB;

# 添加数据
INSERT INTO user(username, password) VALUES('name', 'password');
```

3. 常见问题

- 使用非 root 用户登录，注意用户远程连接、用户权限和防火墙端口。

### 运行服务器

1. 修改 `main.cpp` 中数据库初始化信息

```c++
std::string user = "name";			// 数据库登录用户名
std::string password = "password";	// 数据库登录密码
std::string dbname = "webserv";		// 使用的数据库名称
```

2. 构建并运行

```bash
$ make server
$ ./server
```

3. 浏览器端访问

```bash
http://<ip>:9190/
```

4. 个性化运行

```bash
$ ./server [-p port] [-t thread_number] [-c close_log]
```

- `-p`，自定义端口号，默认为 9190
- `-t`，自定义线程池中线程数量，默认为 8
- `-c`，选择关闭日志，默认打开
	- `0`，打开日志
	- `1`，关闭日志

**运行示例**：
```bash
$ ./server -p 1004 -t 10 -c 1
```
- 端口：1004
- 线程池中线程数量：10
- 服务器日志：关闭

## 测试

1. 服务器环境

- Linux Ubuntu22.04 5.15.0-83-generic
- MySQL  Ver 8.0.41-0ubuntu0.22.04.1
- 云服务器ECS：2核(vCPU) 2 GiB

2. 浏览器环境

- Windows，Chrome 浏览器
- Linux，FireFoox 浏览器 

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

## 参考

1. GitHub 开源项目 [TinyWebServer]( https://github.com/qinguoyi/TinyWebServer) ；
2. TCP/IP 网络编程，尹圣雨 著；
3. Linux 高性能服务器编程， 游双 著.
