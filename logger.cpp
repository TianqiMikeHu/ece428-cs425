#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <ctime>
#include <iostream>
 
#define PORT "5678"
#define ADDRESS "127.0.0.1"
#define MSGSIZE 300

using namespace std;

int main(void){
    int fd, childfd;
    struct addrinfo hints, *servinfo;
    struct sockaddr_storage storage;
    // char ip6[INET6_ADDRSTRLEN];
    socklen_t sin_size;
    int yes = 1;
    setbuf(stdout, NULL);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    // hints.ai_flags = AI_PASSIVE;

    if(getaddrinfo(ADDRESS, PORT, &hints, &servinfo) != 0){
        fprintf(stderr, "getaddrinfo");
        exit(1);
    }

    // inet_ntop(AF_INET, servinfo->ai_addr, ip6, INET6_ADDRSTRLEN);
    // printf("My IPv6 address is: %s\n", ip6);

    if((fd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol)) == -1){
        fprintf(stderr, "socket");
        exit(1);
    }
    if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1){
        fprintf(stderr, "setsockopt");
        exit(1);
    }
    if(bind(fd, servinfo->ai_addr, servinfo->ai_addrlen) == -1){
        fprintf(stderr, "bind");
        exit(1);
    }
    freeaddrinfo(servinfo);
    if(listen(fd, 10) == -1){
        fprintf(stderr, "listen");
        exit(1);
    }

    printf("logger %s\n", PORT);
    while(1){
        sin_size = sizeof storage;
        childfd = accept(fd, (struct sockaddr *)&storage, &sin_size);
        if(childfd == -1){
            fprintf(stderr, "accept");
            continue;
        }
        time_t now = time(0);
        cout << now << " - connected" << endl;
        if(!fork()){
            close(fd);
            char buf[MSGSIZE];
            int num;
            while((num = recv(childfd, buf, MSGSIZE-1, 0)) > 0){
                printf("%s\n", buf);
            }
            cout << time(0) << " disconnected" << endl;
            close(childfd);
            exit(0);
        }
        close(childfd);
    }

}