#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <fstream>
#include <iostream>
#include <map>
#include <limits>
#include <ios>
#include <vector>
#include <mutex>
#include <algorithm>
#include "messages.hpp"
#include <queue>
#include <pthread.h>

using namespace std;
using namespace chrono;

#define timeout 8000

// Get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// Return a listening socket
int get_listener_socket(char* port)
{
    int listener;     // Listening socket descriptor
    int yes=1;        // For setsockopt() SO_REUSEADDR, below
    int rv;

    struct addrinfo hints, *ai, *p;

    // Get us a socket and bind it
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if ((rv = getaddrinfo(NULL, port, &hints, &ai)) != 0) {
        fprintf(stderr, "selectserver: %s\n", gai_strerror(rv));
        exit(1);
    }
    
    for(p = ai; p != NULL; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0) { 
            continue;
        }
        
        // Lose the pesky "address already in use" error message
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener);
            continue;
        }

        break;
    }

    freeaddrinfo(ai); // All done with this

    // If we got here, it means we didn't get bound
    if (p == NULL) {
        return -1;
    }

    // Listen
    if (listen(listener, 10) == -1) {
        return -1;
    }

    return listener;
}

// Add a new file descriptor to the set
void add_to_pfds(struct pollfd *pfds[], int newfd, int *fd_count, int *fd_size)
{
    // If we don't have room, add more space in the pfds array
    if (*fd_count == *fd_size) {
        *fd_size *= 2; // Double it

        *pfds = (pollfd*)realloc(*pfds, sizeof(**pfds) * (*fd_size));
    }

    (*pfds)[*fd_count].fd = newfd;
    (*pfds)[*fd_count].events = POLLIN; // Check ready-to-read

    (*fd_count)++;
}

// Remove an index from the set
void del_from_pfds(struct pollfd pfds[], int i, int *fd_count)
{
    // Copy the one from the end over this one
    pfds[i] = pfds[*fd_count-1];

    (*fd_count)--;
}

void printmessage(message inbox){
    cout <<"\nstatus:"<< inbox.status << " " << endl;
    cout <<"ID:"<< inbox.ID << " " << endl;
    cout <<"priority:"<< inbox.priority << " " << endl;
    cout <<"tiebreaker:"<< inbox.tiebreaker << " " << endl;
    cout <<"action:"<< inbox.action << " " << endl;
    cout <<"originator:"<< inbox.originator << " " << endl;
    cout <<"destination:"<< inbox.destination << " " << endl;
    cout <<"amount:"<< inbox.amount << " " << endl << endl;
}

void summary(message inbox){
    cout <<"\nsta:"<< inbox.status << " ";
    cout <<"ID:"<< inbox.ID << " ";
    cout <<"pri:"<< inbox.priority << " ";
    cout <<"tie:"<< inbox.tiebreaker << " ";
    cout <<"act:"<< inbox.action << " ";
    cout <<"ori:"<< inbox.originator << " ";
    cout <<"des:"<< inbox.destination << " ";
    cout <<"$:"<< inbox.amount << " " << endl;
}

mutex mtx;
mutex box_lock;
mutex message_q_lock;
mutex write_lock;
mutex communication_lock;
map<string, int> accounts;
map<int, string> clientID;
vector<message> message_queue;
queue<message> mailbox;
vector<multicast> targets;
int num_connections;
int timestamp = 0;

void* myThread(void* name){
    int initializing = 1;
    if (initializing){
        for(auto &t: targets){
            struct addrinfo hints, *servinfo;
            memset(&hints, 0, sizeof hints);
            memset(&servinfo, 0, sizeof servinfo);
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            if(getaddrinfo((t.address).c_str(), (t.port).c_str(), &hints, &servinfo) != 0){
                fprintf(stderr, "getaddrinfo");
                exit(1);
            }
            if((t.sock = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol)) == -1){
                fprintf(stderr, "socket");
                exit(1);
            }
            cout << "Attempting to connect to " << t.ID << endl;
            while(connect(t.sock, servinfo->ai_addr, servinfo->ai_addrlen) == -1);
            cout << "Connected to " << t.ID << endl;
            freeaddrinfo(servinfo);
        }
        initializing = 0;
    }

    char sendbuf[sizeof(message)];
    message out;
    // cin.clear();
    // cin.ignore(numeric_limits<streamsize>::max(), '\n');
    // while(cin.get()!=EOF);
    // cout << "Everything flushed" << endl;
    while(1){
        box_lock.lock();
        while(!mailbox.empty()){
            out = mailbox.front();
            mailbox.pop();
            memset(sendbuf, 0, sizeof(message));
            memcpy(sendbuf, &out, sizeof(message));
            communication_lock.lock();
            for(auto &t: targets){
                send(t.sock, (void*)(sendbuf), sizeof(message), 0);
            }
            communication_lock.unlock();
            // write_lock.lock();
            // cout << "out######################" << endl;
            // summary(out);
            // // for(i=0; i<176; i++){
            // //     printf("%x", sendbuf[i]);
            // //     cout<<"/";
            // // }
            // cout << "\nout......................."<<endl;
            // write_lock.unlock();
        }
        box_lock.unlock();
    }
}

void* GenerateEvents(void* name){
    char action[10];
    char originator[30];
    char destination[30];
    char number[30];
    while(1){
        memset(action, '\0', sizeof(char) * 10);
        memset(originator, '\0', sizeof(char) * 30);
        memset(destination, '\0', sizeof(char) * 30);
        memset(number, '\0', sizeof(char) * 30);
        scanf("%s", action);
        // cout << action << " ";
        string action_str(action);
        map<string, int>::iterator it;
        map<string, int>::iterator it2;
        int money;
        string s = "NOT AVAILABLE";
        if(action_str == "DEPOSIT"){
            scanf("%s", originator);
            string originator_str(originator);
            scanf("%s", number);
            message m;
            memset(&m, '\0', sizeof(message));
            m.status = 0;
            strncpy(m.action, action, 10);
            strncpy(m.originator, originator, 30);
            strncpy(m.destination, s.c_str(), 30);
            strncpy(m.tiebreaker, s.c_str(), 30);
            m.priority = INT32_MIN;
            m.amount = stoi(number);
            m.replies = 0;
            strncpy(m.ID, (char*)name, 30);
            mtx.lock();
            timestamp++;
            strncat(m.ID, ("_"+to_string(timestamp)).c_str(), 20);
            mtx.unlock();
            m.now = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
            box_lock.lock();
            mailbox.push(m);
            box_lock.unlock();
            // write_lock.lock();
            // cout << "---NEW EVENT: sending proposal " << m.ID<<endl;
            // cout << "out######################" << endl;
            // printmessage(m);
            // for(i=0; i<176; i++){
            //     printf("%x", sendbuf[i]);
            //     cout<<"/";
            // }
            // cout << "\nout......................."<<endl;
            // write_lock.unlock();
        }
        else if(action_str == "TRANSFER"){
            scanf("%s", originator);
            string originator_str(originator);
            it = accounts.find(originator_str);
            scanf("%s", destination);
            memset(destination, '\0', sizeof(char) * 30);
            scanf("%s", destination);
            string destination_str(destination);
            scanf("%s", number);
            money = stoi(number);
            if(it==accounts.end()){
                continue;
            }
            else if(it->second < money){
                continue;
            }
            else{
                message m;
                memset(&m, '\0', sizeof(message));
                m.status = 0;
                strncpy(m.action, action, 10);
                strncpy(m.originator, originator, 30);
                strncpy(m.destination, destination, 30);
                strncpy(m.tiebreaker, s.c_str(), 30);
                m.priority = INT32_MIN;
                m.amount = stoi(number);
                m.replies = 0;
                strncpy(m.ID, (char*)name, 30);
                mtx.lock();
                timestamp++;
                strncat(m.ID, ("_"+to_string(timestamp)).c_str(), 20);
                mtx.unlock();
                m.now = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
                box_lock.lock();
                mailbox.push(m);
                box_lock.unlock();
                // write_lock.lock();
                // cout << "---NEW EVENT: sending proposal " << m.ID<<endl;
                // cout << "out######################" << endl;
                // printmessage(m);
                // for(i=0; i<176; i++){
                //     printf("%x", sendbuf[i]);
                //     cout<<"/";
                // }
                // cout << "\nout......................."<<endl;
                // write_lock.unlock();
                }
            //  cout << "\tBALANCE: ";
            //  for(it = accounts.begin(); it != accounts.end(); it++){
            //      cout << it->first << ":" << it -> second << " ";
            //  }
            //  cout << endl;
        }
    }
}

// Main
int main(int argc, char** argv)
{
    // argv[1]: ID
    // argv[2]: PORT
    // argv[3]: CONFIG
    if(argc != 4){
        cout << "argument error" << endl;
        exit(1);
    }
    fflush(stdout);
    
    int i;
    ifstream infile;
    char lines[100];
    infile.open(argv[3]);
    infile.getline(lines, 100);
    num_connections = stoi(lines);
    for(i=0; i<num_connections; i++){
        infile.getline(lines, 100);
        string temp(lines);
        multicast con;
        size_t index = temp.find(' ');
        con.ID = temp.substr(0, index);
        temp = temp.substr(index+1);
        index = temp.find(' ');
        con.address = temp.substr(0, index);
        temp = temp.substr(index+1);
        index = temp.find('\r');
        con.port = temp.substr(0, index);
        targets.push_back(con);
    }

    // for(i=0; i<num_connections; i++){
    //     cout<< targets[i].ID << " " << targets[i].address << " " << targets[i].port << endl;
    //     char test[30];
    //         char test2[30];
    //         strncpy(test, (targets[i].address).c_str(), 30);
    //         for(auto &c: test){
    //             printf("%x", c);
    //             printf("/");
    //         }
    //         printf("\n");
    //         strncpy(test2, (targets[i].port).c_str(), 30);
    //         for(auto &c: test2){
    //             printf("%x", c);
    //             printf("/");
    //         }
    // }
    struct sigaction sa;
    if(sigaction(SIGCHLD, &sa, NULL) == -1){
        fprintf(stderr, "sigaction");
        exit(1);
    }

    int listener;     // Listening socket descriptor

    int newfd;        // Newly accepted socket descriptor
    struct sockaddr_storage remoteaddr; // Client address
    socklen_t addrlen;

    char buf[sizeof(message)];    // Buffer for client data

    char remoteIP[INET6_ADDRSTRLEN];

    // Start off with room for 5 connections
    // (We'll realloc as necessary)
    int fd_count = 0;
    int fd_size = 8;
    struct pollfd *pfds = (pollfd*)malloc(sizeof *pfds * fd_size);

    // Set up and get a listening socket
    listener = get_listener_socket(argv[2]);

    if (listener == -1) {
        fprintf(stderr, "error getting listening socket\n");
        exit(1);
    }

    // Add the listener to set
    pfds[0].fd = listener;
    pfds[0].events = POLLIN; // Report ready to read on incoming connection

    fd_count = 1; // For the listener
    
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, myThread, argv[1]);
    pthread_t thread_id2;
    pthread_create(&thread_id2, NULL, GenerateEvents, argv[1]);

    // Main loop
    int poll_count;
    while(1) {
        poll_count = poll(pfds, fd_count, -1);

        if (poll_count == -1) {
            perror("poll_count");
            exit(1);
        }
        else if (poll_count == 0){
            continue;
        }

        // Run through the existing connections looking for data to read
        for(int i = 0; i < fd_count; i++) {

            // Check if someone's ready to read
            if (pfds[i].revents & POLLIN) { // We got one!!

                if (pfds[i].fd == listener) {
                    // If listener is ready to read, handle new connection

                    addrlen = sizeof remoteaddr;
                    newfd = accept(listener,
                        (struct sockaddr *)&remoteaddr,
                        &addrlen);

                    if (newfd == -1) {
                        perror("accept");
                    } else {
                        add_to_pfds(&pfds, newfd, &fd_count, &fd_size);

                        write_lock.lock();
                        printf("pollserver: new connection from %s on "
                            "socket %d\n",
                            inet_ntop(remoteaddr.ss_family,
                                get_in_addr((struct sockaddr*)&remoteaddr),
                                remoteIP, INET6_ADDRSTRLEN),
                            newfd);
                        write_lock.unlock();
                    }
                } else {
                    // If not the listener, we're just a regular client
                    memset(buf, 0, sizeof(message));
                    int nbytes = recv(pfds[i].fd, buf, sizeof buf, 0);

                    int sender_fd = pfds[i].fd;

                    if (nbytes <= 0) {
                        // Got error or connection closed by client
                        if (nbytes == 0) {
                            // Connection closed
                            // FAILURE HANDLER STARTS
                            communication_lock.lock();
                            num_connections--;
                            vector<multicast>::iterator iter;
                            map<int, string>::iterator iter2 = clientID.find(sender_fd);
                            string failed = iter2->second;
                            for(iter = targets.begin(); iter!=targets.end(); iter++){
                                if(iter->ID == failed){
                                    targets.erase(iter);
                                    break;
                                }
                            }
                            communication_lock.unlock();
                            write_lock.lock();
                            // printf("pollserver: socket %d hung up\n", sender_fd);
                            cout << "FAILURE DETECTED: " << failed << endl;
                            write_lock.unlock();
                            message_q_lock.lock();
                            // sort(message_queue.begin(), message_queue.end(), Compare);
                            // write_lock.lock();
                            // cout << "---------SORTED--------------" << endl;
                            // for(auto & msg: message_queue){
                            //     printmessage(msg);
                            // }
                            // cout << "----------END SORTED-------------" << endl;
                            // write_lock.unlock();
                            // vector<message>::iterator iter3 = message_queue.begin();
                            // while(iter3!=message_queue.end()){
                            //     string node_name(iter3->ID);
                            //     size_t index = node_name.find('_');
                            //     node_name = node_name.substr(0, index);
                            //     if(iter3->status == 1 && node_name == failed){
                            //         message_queue.erase(iter3);
                            //     }
                            //     else{
                            //         iter3++;
                            //     }
                            // }
                            message_queue.clear();
                            message_q_lock.unlock();
                        } else {
                            perror("recv");
                        }

                        close(pfds[i].fd); // Bye!

                        del_from_pfds(pfds, i, &fd_count);

                    } 
                    else {
                        // PROCESS MESSAGE HERE
                        // cout << "\t\tsize: "<<sizeof(message) <<endl;
                        message inbox;
                        message temp;
                        memset(&inbox, '\0', sizeof(message));
                        memcpy(&inbox, buf, sizeof(message));
                        // write_lock.lock();
                        // cout << "~~~~~~~~~~~~~~~~~~~~~~~~~" << endl;
                        // summary(inbox);
                        // // for(i=0; i<176; i++){
                        // //     printf("%x", buf[i]);
                        // //     cout<<"/";
                        // // }
                        // cout<< "\n__________________________"<< endl;
                        // write_lock.unlock();
                        // inbox.status = buf[0] + (buf[1] << 8) + (buf[2] << 16) + (buf[3] << 24);
                        // strncpy(inbox.ID, buf+4, 50);
                        // inbox.priority = buf[54] + (buf[55] << 8) + (buf[56] << 16) + (buf[57] << 24);
                        // strncpy(inbox.tiebreaker, buf+58, 30);
                        // strncpy(inbox.action, buf+88, 10);
                        // strncpy(inbox.originator, buf+98, 30);
                        // strncpy(inbox.destination, buf+128, 30);
                        // inbox.amount = buf[158] + (buf[159] << 8) + (buf[160] << 16) + (buf[161] << 24);
                        // inbox.deliver = bool(buf[162]);
                        
                        string node_name(inbox.ID);
                        size_t index = node_name.find('_');
                        node_name = node_name.substr(0, index);
                        string myname(argv[1]);
                        map<int, string>::iterator lookup;
                        bool found;
                        bool deliver;
                        // write_lock.lock();
                        // /cout << "We got a message from " << inbox.ID << ", status code " << inbox.status << " ";
                        // write_lock.unlock();
                        switch (inbox.status){
                        case 0:
                            // write_lock.lock();
                            // /cout << "Received Proposal" << endl;
                            // write_lock.unlock();
                            strncpy(inbox.tiebreaker, argv[1], 30);
                            inbox.status = 1;
                            mtx.lock();
                            timestamp++;
                            inbox.priority = timestamp;
                            mtx.unlock();
                            message_q_lock.lock();
                            message_queue.push_back(inbox);
                            message_q_lock.unlock();
                            box_lock.lock();
                            mailbox.push(inbox);
                            box_lock.unlock();
                            lookup = clientID.find(sender_fd);
                            if(lookup==clientID.end()){
                                clientID[sender_fd] = node_name;
                            }
                            // write_lock.lock();
                            // cout << "Reply queued to be sent"<<endl;
                            // write_lock.unlock();
                            break;
                        
                        case 1:
                            if(node_name!=myname){
                                break;
                            }
                            // write_lock.lock();
                            // /cout << "Received Reply" << endl;
                            // write_lock.unlock();
                            found = false;
                            deliver = false;
                            message_q_lock.lock();
                            for(auto & msg: message_queue){
                                if(found){
                                    break;
                                }
                                if(strcmp(msg.ID, inbox.ID)==0){
                                    found = true;
                                    if(inbox.priority>msg.priority){
                                        msg.priority = inbox.priority;
                                        memset(msg.tiebreaker, '\0', 30);
                                        strncpy(msg.tiebreaker, inbox.tiebreaker, 30);
                                    }
                                    else if(inbox.priority==msg.priority && inbox.tiebreaker>msg.tiebreaker){
                                        memset(msg.tiebreaker, '\0', 30);
                                        strncpy(msg.tiebreaker, inbox.tiebreaker, 30);
                                    }
                                    mtx.lock();
                                    if(timestamp<inbox.priority){
                                        timestamp = inbox.priority;
                                    }
                                    mtx.unlock();
                                    msg.replies++;
                                    communication_lock.lock();
                                    if(msg.replies == num_connections){
                                        deliver = true;
                                        msg.status = 2;
                                        box_lock.lock();
                                        temp = msg;
                                        mailbox.push(temp);
                                        box_lock.unlock();
                                        // write_lock.lock();
                                        // cout << "Final broadcast queued" << endl;
                                        // write_lock.unlock();
                                    }
                                    communication_lock.unlock();
                                }
                            }
                            if(!found){
                                temp = inbox;
                                temp.status = 0;
                                temp.replies = 1;
                                communication_lock.lock();
                                if(temp.replies == num_connections){
                                    deliver = true;
                                    temp.status = 2;
                                    box_lock.lock();
                                    mailbox.push(temp);
                                    box_lock.unlock();
                                    // write_lock.lock();
                                    // cout << "Final broadcast queued" << endl;
                                    // write_lock.unlock();
                                }
                                communication_lock.unlock();
                                message_queue.push_back(temp);
                            }
                            message_q_lock.unlock();
                            if(!deliver){
                                break;
                            }
                            message_q_lock.lock();
                            sort(message_queue.begin(), message_queue.end(), Compare);
                            // write_lock.lock();
                            // cout << "---------SORTED--------------" << endl;
                            // for(auto & msg: message_queue){
                            //     printmessage(msg);
                            // }
                            // cout << "----------END SORTED-------------" << endl;
                            // write_lock.unlock();
                            while(!message_queue.empty()){
                                map<string, int>::iterator it;
                                map<string, int>::iterator it2;
                                if(message_queue[0].status==2){
                                    message deliver = message_queue[0];
                                    mtx.lock();
                                    if(timestamp < deliver.priority){
                                        timestamp = deliver.priority;
                                    }
                                     mtx.unlock();
                                     message_queue.erase(message_queue.begin());
                                    string action_str(deliver.action);
                                    string originator_str(deliver.originator);
                                    if(action_str == "DEPOSIT"){
                                        it = accounts.find(originator_str);
                                        if(it!=accounts.end()){
                                            it->second += deliver.amount;
                                        }
                                        else{
                                            accounts[originator_str] = deliver.amount;
                                        }
                                    }
                                    else if(action_str == "TRANSFER"){
                                        string destination_str(deliver.destination);
                                        it = accounts.find(originator_str);
                                        it2 = accounts.find(destination_str);
                                        if(it2!=accounts.end()){
                                            it2->second += deliver.amount;
                                            it->second -= deliver.amount;
                                        }
                                        else{
                                            accounts[destination_str] = deliver.amount;
                                            it->second -= deliver.amount;
                                        }
                                    }
                                    write_lock.lock();
                                    cout << "BALANCES ";
                                    for(it = accounts.begin(); it != accounts.end(); it++){
                                        cout << it->first << ":" << it -> second << " ";
                                    }
                                    cout << endl;
                                    fflush(stdout);
                                    write_lock.unlock();
                                }
                                else{
                                    break;
                                }
                            }
                            message_q_lock.unlock();
                            break;
                        
                        case 2:
                            // write_lock.lock();
                            // cout << "Received Final Priority" << endl;
                            // write_lock.unlock();
                            found = false;
                            message_q_lock.lock();
                            for(auto & msg: message_queue){
                                if(found){
                                    break;
                                }
                                if(strcmp(msg.ID, inbox.ID)==0){
                                    found = true;
                                    msg.priority = inbox.priority;
                                    memset(msg.tiebreaker, '\0', 30);
                                    strncpy(msg.tiebreaker, inbox.tiebreaker, 30);
                                    msg.status = 2;
                                }
                            }
                            sort(message_queue.begin(), message_queue.end(), Compare);
                            // write_lock.lock();
                            // cout << "---------SORTED--------------" << endl;
                            // for(auto & msg: message_queue){
                            //     printmessage(msg);
                            // }
                            // cout << "----------END SORTED-------------" << endl;
                            // write_lock.unlock();
                            while(!message_queue.empty()){
                                map<string, int>::iterator it;
                                map<string, int>::iterator it2;
                                if(message_queue[0].status==2){
                                    message deliver = message_queue[0];
                                    mtx.lock();
                                    if(timestamp < deliver.priority){
                                        timestamp = deliver.priority;
                                    }
                                    mtx.unlock();
                                    message_queue.erase(message_queue.begin());
                                    string action_str(deliver.action);
                                    string originator_str(deliver.originator);
                                    if(action_str == "DEPOSIT"){
                                        it = accounts.find(originator_str);
                                        if(it!=accounts.end()){
                                            it->second += deliver.amount;
                                        }
                                        else{
                                            accounts[originator_str] = deliver.amount;
                                        }
                                    }
                                    else if(action_str == "TRANSFER"){
                                        string destination_str(deliver.destination);
                                        it = accounts.find(originator_str);
                                        it2 = accounts.find(destination_str);
                                        if(it2!=accounts.end()){
                                            it2->second += deliver.amount;
                                            it->second -= deliver.amount;
                                        }
                                        else{
                                            accounts[destination_str] = deliver.amount;
                                            it->second -= deliver.amount;
                                        }
                                    }
                                     write_lock.lock();
                                     cout << "BALANCES ";
                                     for(it = accounts.begin(); it != accounts.end(); it++){
                                         cout << it->first << ":" << it -> second << " ";
                                     }
                                     cout << endl;
                                     fflush(stdout);
                                     write_lock.unlock();
                                }
                                else{
                                    break;
                                }
                            }
                            message_q_lock.unlock();
                            break;
                        
                        default:
                            cout << "not sure why we are here" << endl;
                            // printmessage(inbox);
                            break;
                        }
                    }
                } // END handle data from client
            } // END got ready-to-read from poll()
        } // END looping through file descriptors
    } // END while
    
    return 0;
}