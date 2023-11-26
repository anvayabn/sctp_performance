#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/sctp.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>

#define MAX_EVENTS 512
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

/* socket */
int sockfd[MAX_EVENTS] = {-1}; 
int connected[MAX_EVENTS] = {-1};
int epolfd;

/* socket flags */
int dom = AF_INET; 
int type = SOCK_STREAM; 
int protocol = IPPROTO_SCTP; 

/* event poll */
#define DEFAULT_BLOCK_TIME 0
struct epoll_event ev_list[MAX_EVENTS];


/* IO buffer */
#define MAX_BUFFER 8192
char input_buffer[MAX_BUFFER]; 
char output_buffer[MAX_BUFFER]; 

int create_socket(int domain, int type, int protocol)
{
    int ret; 
    ret = socket(domain, type, protocol); 
    if (ret == -1 ){
        perror("Socket Create failed\n"); 
    }

    return ret ; 

}

int connect_socket(int socket_fd, int dom, int index) {
    int ret; 
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr)); 
    servaddr.sin_family = dom; 
    servaddr.sin_port = htons(server_port); 
    servaddr.sin_addr.s_addr = inet_addr(server_ip); 

    ret = connect(socket_fd, (struct sockaddr *)&servaddr, sizeof(servaddr)); 
    if (ret < 0) {
        if (errno == EINPROGRESS) {
            connected[index] = socket_fd;  // Mark the socket as connected
            return 0;  // Return success because this is expected in non-blocking mode
        } else {
            perror("Connect failed\n");
            return ret; 
        }
    }
    connected[index] = socket_fd;
    return ret; 
}


int make_socket_non_blocking(int sfd) {
    int flags = fcntl(sfd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        return -1;
    }

    flags |= O_NONBLOCK;
    if (fcntl(sfd, F_SETFL, flags) == -1) {
        perror("fcntl F_SETFL");
        return -1;
    }

    return 0;
}

void add_to_epoll_set(int epollfd, int not){
    for (int i = 0 ; i < not; i++){
        if (connected[i] > 0){
            struct epoll_event ev;
            ev.events = EPOLLIN;
            ev.data.fd = connected[i];
            epoll_ctl(epolfd, EPOLL_CTL_ADD, connected[i], &ev);
        }
    }

}

void server() {
    printf("Server starting : Ip : %s, Port : %d\n", server_ip, server_port); 

    int listen_fd = create_socket(dom, type, protocol);
    if ( listen_fd < 0 ){
        printf("Socket Create failed\n"); 
        exit(1);
    }

    /* make it non blocking */
    make_socket_non_blocking(listen_fd);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(server_ip);
    addr.sin_port = htons(server_port);

    int ret; 
    ret = bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr));
    if ( ret < 0 ){
        printf("Bind failed\n"); 
        exit(1); 
    }

    ret = listen(listen_fd, MAX_EVENTS);
    if (ret < 0 ){
        printf("Listen failed\n"); 
        exit(1); 
    }

    int epoll_fd = epoll_create(MAX_EVENTS);
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = listen_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &event);

    while (1) {
        struct epoll_event events[MAX_EVENTS];
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, 0);
        if (n < 0 )
        {
            printf("Epoll failed \n"); 
            break; 
        }
        
        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == listen_fd) {
                printf("got connection \n"); 
                int conn_fd = accept(listen_fd, NULL, NULL);
                make_socket_non_blocking(conn_fd);
                event.data.fd = conn_fd;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_fd, &event);
            } else {
                if (events[i].events & EPOLLIN){
                    int sfd = events[i].data.fd; 
                    bzero(input_buffer, MAX_BUFFER);
                    size_t readlen = recv(sfd, input_buffer, sizeof(input_buffer), 0); 
                    size_t sendlen = send(sfd, output_buffer, message_size, 0); 
                }
            }
        }
    }   
} 

void cleanup() {
    for (int i = 0; i < MAX_EVENTS; i++) {
        if (sockfd[i] != -1) {
            close(sockfd[i]);
            sockfd[i] = -1;
        }
    }
}

int client(int not){
    printf("Client Starting....\n");

    epolfd = epoll_create(MAX_EVENTS);
    
    for ( int i = 0 ; i < not ; i ++){
        sockfd[i] = create_socket(dom, type, protocol);

        make_socket_non_blocking(sockfd[i]); 

        connect_socket(sockfd[i], dom, i);

    }

    /* add connected sockets to epoll-set*/
    add_to_epoll_set(epolfd, not); 

    int start = 1 ; 
    time_t start_time = time(NULL); 

    while (start) {

        int ret = epoll_wait(epolfd, ev_list, MAX_EVENTS, DEFAULT_BLOCK_TIME); 
        
        time_t current_time = time(NULL);
        if (difftime(current_time, start_time) >= duration) {
            break;
        }

        /* there is a fault */
        if (ret < 0 ){
            printf("Fault at epoll\n"); 
        }else if (ret > 0 ){
            /* do all the reading here */
            int nconnections = ret;
            for (int i = 0 ; i < nconnections; i ++ ){
                if (ev_list[i].events & EPOLLIN){
                    int sfd =  ev_list[i].data.fd;
                    bzero(input_buffer, MAX_BUFFER); 
                    size_t read_len = recv(sfd, input_buffer, sizeof(input_buffer), 0); 
                }
            }
             
        }

        /* if nothing to read just keep sending data */
        for ( int i = 0 ; i < not ; i ++){
            if (connected[i] > 0){
                size_t send_len = send(connected[i], output_buffer, message_size, 0);
            }
        }

    }

    cleanup(); 

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

    int number_of_conn = num_threads ;

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
