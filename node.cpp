#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string>

#define MSGSIZE 300
#define TIMESIZE 50
#define TEXTSIZE 200

using namespace std;

int main(int argc, char** argv){
    // argv[1]: name
    // argv[2]: address
    // argv[3]: port
    int fd;
    char message[MSGSIZE];
    char time[TIMESIZE];
    char text[TEXTSIZE];
    struct addrinfo hints, *servinfo;
    setbuf(stdout, NULL);

    if(argc != 4){
        exit(1);
    }


    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if(getaddrinfo(argv[2], argv[3], &hints, &servinfo) != 0){
        fprintf(stderr, "getaddrinfo");
        exit(1);
    }

    if((fd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol)) == -1){
        fprintf(stderr, "socket");
        exit(1);
    }
    if(connect(fd, servinfo->ai_addr, servinfo->ai_addrlen) == -1){
        fprintf(stderr, "connect");
        printf("%d", errno);
        exit(1);
    }
    freeaddrinfo(servinfo);

    while(1){
        memset(time, '\0', sizeof(char) * TIMESIZE);
        memset(text, '\0', sizeof(char) * TEXTSIZE);
        scanf("%s", time);
        scanf("%s", text);

        // string msg(message);
        // string out(argv[1]);
        // out += ' ';
        // int pos = msg.find(' ');
        // out += msg.substr(0, pos);
        // out += ' ';
        // msg.erase(0, pos+1);
        // out += msg;
        // memset(message, '\0', sizeof(char) * MSGSIZE);
        // strncpy(message, out.c_str(), out.length());
        // printf("%s\n", message);

        string name(argv[1]);
        string timestring(time);
        string textstring(text);
        name  = name + ' ' + time + ' ' + text;

        memset(message, '\0', sizeof(char) * MSGSIZE);
        strncpy(message, name.c_str(), name.length());

        send(fd, message, MSGSIZE, 0);
    }
    return 0;


}