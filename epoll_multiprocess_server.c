#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/sysinfo.h>
#include <sys/epoll.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/types.h>
#include "httpd.h"

#define DEFAULT_PORT 8080
#define MAX_EVENT_NUM 1024
#define INFTIM -1

void process(int);

void handle_subprocess_exit(int);

int main(int argc, char *argv[]){
    
    struct sockaddr_in server_addr;
    int listen_fd;
    int cpu_core_num;
    int on = 1;

    //创建套接字
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    //设置为非阻塞
    fcntl(listen_fd, F_SETFL, O_NONBLOCK);
    //设置端口复用
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    //声明并初始化服务端的socket地址结构
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(DEFAULT_PORT);

    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind error, message: ");
        exit(1);
    }

    if (listen(listen_fd, 5) == -1) {
        perror("listen error, message: ");
        exit(1);
    }

    printf("listening 8080\n");
    //信号处理
    signal(SIGCHLD, handle_subprocess_exit);
    //获取核数并打印
    cpu_core_num = get_nprocs();
    printf("cpu core num: %d\n", cpu_core_num);

    //创建子进程
    for (int i = 0; i < cpu_core_num * 2; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            process(listen_fd);
            exit(0);
        }
    }

    while (1) {
        sleep(1);
    }

    return 0;
}

void process(int listen_fd){
    int conn_fd;
    int ready_fd_num;
    struct sockaddr_in client_addr;
    int client_addr_size = sizeof(client_addr);
    char buf[128];

    struct epoll_event ev, events[MAX_EVENT_NUM];
    //创建epoll句柄，监听数目
    int epoll_fd = epoll_create(MAX_EVENT_NUM);

    //设置与要处理的事件相关的文件描述符
    ev.data.fd = listen_fd;
    //设置要处理的事件类型
    ev.events = EPOLLIN;
    //epoll的事件注册函数
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev) == -1) {
        perror("epoll_ctl error, message: ");
        exit(1);
    }

    while(1){
        //等待事件触发，当超过timeout还没有事件触发时，就超时
        ready_fd_num = epoll_wait(epoll_fd, events, MAX_EVENT_NUM, INFTIM);
        printf("[pid %d] 进程被唤醒...\n", getpid());
        
        if (ready_fd_num == -1) {
            perror("epoll_wait error, message: ");
            continue;
        }

        for(int i = 0; i < ready_fd_num; i++) {
            if (events[i].data.fd == listen_fd) {
                //用于从已完成连接队列返回下一个已完成连接
                conn_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_addr_size);
                if (conn_fd == -1) {
                    sprintf(buf, "[pid %d] accept 出错: ", getpid());
                    perror(buf);
                    continue;
                }

                if (fcntl(conn_fd, F_SETFL, fcntl(conn_fd, F_GETFD, 0) | O_NONBLOCK) == -1) {
                    continue;
                }
                ev.data.fd = conn_fd;
                ev.events = EPOLLIN;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_fd, &ev) == -1) {
                    perror("epoll_ctl error, message: ");
                    close(conn_fd);
                }
                printf("[pid %d] 📡 收到来自 %s:%d 的请求\n", getpid(), inet_ntoa(client_addr.sin_addr), client_addr.sin_port);
            }else if(events[i].events & EPOLLIN){
                printf("[pid %d] ✅ 处理来自 %s:%d 的请求\n", getpid(), inet_ntoa(client_addr.sin_addr), client_addr.sin_port);
                conn_fd = events[i].data.fd;
                accept_request(conn_fd, &client_addr);
                close(conn_fd);
            }else if (events[i].events & EPOLLERR){
                fprintf(stderr, "epoll error\n");
                close(conn_fd);
            }
        }
    }
}

void handle_subprocess_exit(int signo){
    printf("clean subprocess.\n");
    int status;  
    while(waitpid(-1, &status, WNOHANG) > 0);
}