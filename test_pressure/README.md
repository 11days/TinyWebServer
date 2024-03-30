<!-- 喳喳辉做项目 -->
# 服务器压力测试
## Webbench 介绍
一个压测软件，可以在命令行通过 sudo apt-get install 安装依赖，以及后续的源码的下载和安装

### 原理

>* webbench 首先 fork 出多个子进程，每个子进程都循环做 web 访问测试。

>* 子进程把访问的结果通过 pipe 告诉父进程，父进程做最终结果的统计

### 详细说明

1）父进程调用 fork() 系统调用时，操作系统会创建一个新的子进程，这个子进程是父进程的一个副本，包括代码，数据以及各种资源和状态。

2）父进程和子进程都会继续执行接下来的指令，但是 fork() 函数返回值不同。

3）具体的说，父进程中，fork() 返回值是新创建的子进程 ID（PID）；而子进程，fork() 返回值是 0

4）返回值的不同，使得父子进程可以分别执行自己的逻辑

5）父进程多次调用 fork() 创建多个子进程，子进程间互相独立，有着自己的进程 ID，同时运行在自己的地址空间里

>* 测试处在相同硬件上，不同服务的性能以及不同硬件上同一个服务的运行状况
>* 每秒钟响应请求数 和 每秒钟传输数据量


## Webbench 使用
<div align=center><img src="https://github.com/11days/TinyWebServer/tree/master/root/pressure_1.png" height="201"/> </div>
<div align=center><img src="https://github.com/11days/TinyWebServer/tree/master/root/pressure_2.png" height="201"/> </div>
<div align=center><img src="https://github.com/11days/TinyWebServer/tree/master/root/pressure_3.png" height="201"/> </div>
看看参数
<div align=center><img src="https://github.com/11days/TinyWebServer/tree/master/root/pressure_4.png" height="201"/> </div>

## 压测结果
<div align=center><img src="https://github.com/11days/TinyWebServer/tree/master/root/pressure_5.png" height="201"/> </div>
<div align=center><img src="https://github.com/11days/TinyWebServer/tree/master/root/pressure_6.png" height="201"/> </div>