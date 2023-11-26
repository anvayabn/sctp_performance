#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/sctp.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>

#define MAX_EVENTS 256
#define DEFAULT_MODE 1 

#define DEFAULT_SERVER_IP "127.0.0.1"
#define DEFAULT_SERVER_PORT 2905
#define DEFAULT_NUM_THREADS 4
#define DEFAULT_MESSAGE_SIZE 1024
#define DEFAULT_DURATION 10

char *server_ip = DEFAULT_SERVER_IP;
int server_port = DEFAULT_SERVER_PORT;
int num_threads = DEFAULT_NUM_THREADS;
int message_size = DEFAULT_MESSAGE_SIZE;
int mode = DEFAULT_MODE; 
int duration  = DEFAULT_DURATION ;

/* socket flags */
int dom = AF_INET; 
int type = SOCK_STREAM; 
int protocol = IPPROTO_SCTP; 

/* event poll */
#define DEFAULT_BLOCK_TIME -1


/* IO buffer */
#define MAX_BUFFER 8192
char input_buffer[MAX_BUFFER]; 
char output_buffer[MAX_BUFFER]; 

void make_socket_non_blocking(int sfd) {
    int flags = fcntl(sfd, F_GETFL, 0);
    if (flags < 0) {
        perror("fcntl");
        exit(EXIT_FAILURE);
    }
    flags |= O_NONBLOCK;
    if (fcntl(sfd, F_SETFL, flags) < 0) {
        perror("fcntl");
        exit(EXIT_FAILURE);
    }
}

int server(){
    {
    int sfd, efd;
    struct sockaddr_in server_addr;
    struct epoll_event event, events[MAX_EVENTS];

    sfd = socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP);
    if (sfd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(3000);
    if (inet_pton(AF_INET, "10.10.1.1", &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        exit(EXIT_FAILURE);
    }

    if (bind(sfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    make_socket_non_blocking(sfd);

    if (listen(sfd, SOMAXCONN) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    efd = epoll_create1(0);
    if (efd == -1) {
        perror("epoll_create");
        exit(EXIT_FAILURE);
    }

    event.data.fd = sfd;
    event.events = EPOLLIN;
    if (epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &event) == -1) {
        perror("epoll_ctl");
        exit(EXIT_FAILURE);
    }

    while (1) {
        int n = epoll_wait(efd, events, MAX_EVENTS, DEFAULT_BLOCK_TIME);
        for (int i = 0; i < n; i++) {
            if ((events[i].events & EPOLLERR) ||
                (events[i].events & EPOLLHUP) ||
                (!(events[i].events & EPOLLIN))) {
                perror("epoll error");
                close(events[i].data.fd);
                continue;
            } else if (sfd == events[i].data.fd) {
                struct sockaddr in_addr;
                socklen_t in_len = sizeof(in_addr);
                int infd = accept(sfd, &in_addr, &in_len);
                if (infd == -1) {
                    perror("accept");
                    continue;
                }
    
                make_socket_non_blocking(infd);
    
                event.data.fd = infd;
                event.events = EPOLLIN;
                if (epoll_ctl(efd, EPOLL_CTL_ADD, infd, &event) == -1) {
                    perror("epoll_ctl");
                    exit(EXIT_FAILURE);
                }
            } else {
                int done = 0;
    
                while (1) {
                    ssize_t count = recv(events[i].data.fd, input_buffer, sizeof(input_buffer), 0);
                    if (count == -1) {
                        if (errno != EAGAIN) {
                            perror("recv");
                            done = 1;
                        }
                        break;
                    } else if (count == 0) {
                        done = 1;
                        break;
                    }

                    ssize_t sent = send(events[i].data.fd, output_buffer, message_size, 0);
                    if (sent == -1) {
                        perror("send");
                        done = 1;
                        break;
                    }
                }
    
                if (done) {
                    printf("Closed connection on descriptor %d\n", events[i].data.fd);
                    close(events[i].data.fd);
                }
            }
        }
    }

    close(sfd);
    return 0;
}

}

int client(int no_conn)
{
    int sockfd[no_conn]; 
    int epfd;
    struct sockaddr_in server_addr;
    struct epoll_event events[MAX_EVENTS * no_conn];
    struct epoll_event event;
    
    epfd = epoll_create1(0);

    for ( int i = 0 ; i < no_conn ; i++){
        if ((sockfd[i] = socket(dom, type, protocol)) < 0){
            perror("Failed creating socket");
            exit(EXIT_FAILURE); 
        }

        make_socket_non_blocking(sockfd[i]);

        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(3000);
        inet_pton(AF_INET, "10.10.1.1", &server_addr.sin_addr);

        if (connect(sockfd[i], (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
            if (errno != EINPROGRESS) {
                perror("connect");
                exit(EXIT_FAILURE);
            }
        }

        event.data.fd = sockfd[i]; 
        event.events = EPOLLIN | EPOLLOUT; 
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd[i], &event) != 0){
            perror("cannot add to epoll list"); 
            exit(EXIT_FAILURE); 
        }
    }

    struct timespec start_time, current_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    long total_bytes_sent = 0;
    long total_bytes_received = 0;

    /* Event loop*/
    while (1){
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        if (current_time.tv_sec - start_time.tv_sec > duration) {
            break;
        }

        int n = epoll_wait(epfd, events, MAX_EVENTS, 1000);
        if (n == 0){
            continue;
        }else if (n < 0){
            perror("epollwait failed");
            exit(EXIT_FAILURE); 
        }

        for (int i = 0 ; i < n ; i ++){
            if ( events[i].events & EPOLLERR ||
                 events[i].events & EPOLLHUP ||
                 !(events[i].events & (EPOLLIN | EPOLLOUT))) 
            {
                perror("epoll event error");
                close(events[i].data.fd);
                continue;
            
            }
            
            if (events[i].events & EPOLLOUT) {
                int fd = events[i].data.fd;
                int error = 0;
                socklen_t errlen = sizeof(error);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &errlen) == -1) {
                    perror("getsockopt");
                    close(fd);
                    continue;
                }   
                if (error != 0) {
                    fprintf(stderr, "Connection failed: %s\n", strerror(error));
                    close(fd);
                    continue;
                }                
                size_t sendlen = send(fd, output_buffer, (size_t)message_size, 0); 
                if (sendlen == -1 ){
                    perror("send failed"); 
                    continue;
                }else if (sendlen== 0){
                    close(fd);
                    continue;
                }
                
                total_bytes_sent += sendlen;

                event.data.fd = events[i].data.fd;
                event.events = EPOLLIN;
                epoll_ctl(epfd, EPOLL_CTL_MOD, events[i].data.fd, &event);
            }

            if (events[i].events & EPOLLIN) {
                ssize_t readlen = recv(events[i].data.fd, input_buffer, sizeof(input_buffer), 0);
                if (readlen== -1) {
                    perror("recv failed");
                    close(events[i].data.fd);
                    continue;
                } else if (readlen == 0) {
                    close(events[i].data.fd);
                    continue;
                }
                total_bytes_received += readlen;
            }

        }


    }

    double throughput = (double)(total_bytes_sent + total_bytes_received) / duration;

    printf("Throughput: %f bytes/sec\n", throughput);

    for (int i = 0; i < no_conn; i++) {
        close(sockfd[i]);
    }
    close(epfd);

    return 0;
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "  Usage: %s [OPTIONS]\n"
        "  Options:\n"
        "  -s, Server Mode \n"
        "  -c, Client Mode \n"
        "  -r, Server IP \n"
        "  -p, Server Port\n"
        "  -t, Number of threads\n"
        "  -m, Message Size\n"  
        "  -d, Duration of measurement\n", prog);
    exit(1);
}

void parse_command_line(int argc, char *argv[]){
    int opt;

    while ((opt = getopt(argc, argv, "sc:r:p:t:m:d:")) != -1){
        switch(opt)
        {
            case 'c':
                /* client mode */
                mode = 0;
                break;
            case 's':
                mode = 1;
                break;
            case 'r':
                server_ip = optarg;
                break;
            case 'p':
                server_port = atoi(optarg);
                break;
            case 't':
                num_threads = atoi(optarg);
                break;
            case 'm':
                message_size = atoi(optarg);
                break;
            case 'd':
                duration = atoi(optarg); 
                break;    
            default:
                usage(argv[0]);
        }
    }
}

int main(int argc, char *argv[])
{
    /* parse the cmd line */
    parse_command_line(argc, argv); 

    int number_of_conn = num_threads;

    /* prepare buffer */
    bzero(output_buffer, message_size); 

    /* if mode is  1 then server or client */
    if (mode == 1 )
    {
        server();
    }else 
    {
        client(number_of_conn);
    }

    return 0;

}
