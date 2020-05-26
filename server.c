//
//  socket.c
//  Test
//
//  Created by junhao hu on 2020/5/20.
//  Copyright © 2020 junhao hu. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <byteswap.h>
#include <endian.h>
#define SIZE (20)
#define SERVER_MSG ("Hello User ! Welcome to my server!\n")
//#include <libkern/OSByteOrder.h>
//#define bswap_16(x) OSSwapInt16(x)
//#define bswap_32(x) OSSwapInt32(x)
//#define bswap_64(x) OSSwapInt64(x)

typedef struct node{
    struct node* next;
    struct connect_data* connect_date;
}node_t;

typedef struct linked_queue{
    struct node *head;
    struct node *tail;
    pthread_mutex_t queue_lock;
    pthread_cond_t queue_con;
    int shutdown_flag;
    char *msg;
}linked_queue_t;

typedef struct b_file{
    uint32_t ip_v4_address;
    uint16_t port;
    char *message;
}b_file_t;


struct connect_data{
    int socket_fd;
    struct linked_queue* queue;
};

typedef struct package{
    unsigned char type_digit;
    unsigned char compression_bit;
    unsigned char require_bit;
}package_t;

typedef struct message{
    struct package header;
    uint64_t pay_load_length;
    char *pay_load;
}message_t;

typedef struct huffman_tree_node{
    uint8_t bytes;
    struct huffman_tree_node *left;
    struct huffman_tree_node *right;
    int is_external;
    
}tree_node_t;

typedef struct huffman_tree{
    struct huffman_tree_node *root;
}huffman_tree_t;

void enqueue(linked_queue_t *queue, struct connect_data* data){
    node_t *newnode = malloc(sizeof(node_t));
    newnode->connect_date = data;
    newnode->next = NULL;
    if (queue->tail == NULL){
        queue->head = newnode;
    } else{
        queue->tail->next = newnode;
    }
    queue->tail = newnode;
}

struct connect_data* dequeue(linked_queue_t *queue){
    if (queue->head == NULL){
        return NULL;
    } else{
        struct connect_data *result = queue->head->connect_date;
        node_t *temp = queue->head;
        queue->head = queue->head->next;
        if (queue->head == NULL) {
            queue->tail = NULL;
        }
        free(temp);
        return result;
    }
}


unsigned char get_first_digit(unsigned int number){
    return number >> 4;
}

unsigned char get_five_digit(unsigned int number){
    number >>= 3;
    return number & 1;
}

unsigned char get_six_digit(unsigned int number){
    number >>= 2;
    return number & 1;
}


unsigned char transform_header(message_t message){
    unsigned char header = (message.header.type_digit << 4) | (message.header.compression_bit << 3) | (message.header.require_bit << 2);
    return header;
}

void *connection_handler(void *argv){
    struct connect_data *data = argv;
    if (!data->queue->shutdown_flag){
        while (1) {
    //        printf("1\n");
             printf("here\n");
            message_t message = {0};
            unsigned char buffer = 0;
            long number = recv(data->socket_fd, &buffer, 1, 0);
            if (number == 0){
                close(data->socket_fd);
                break;
            }
            number = recv(data->socket_fd, &(message.pay_load_length), sizeof(message.pay_load_length), 0);
            if (number == 0){
                close(data->socket_fd);
                break;
            }
            message.header.type_digit = get_first_digit(buffer);
            message.header.compression_bit = get_five_digit(buffer);
            message.header.require_bit = get_six_digit(buffer);
            uint64_t v = message.pay_load_length;
            message.pay_load_length = be64toh(message.pay_load_length);
            message.pay_load = malloc(message.pay_load_length);
    //        printf("type_digit: %x\n",(int)message.header.type_digit);
    //        printf("%x\n",message.pay_load_length);
            if (message.pay_load_length > 0)
                recv(data->socket_fd, message.pay_load, message.pay_load_length, 0);
            if(message.header.type_digit == 0x00){
    //            printf("here echo\n");
                message.header.type_digit = 0x1;
                message.header.compression_bit = 0;
                unsigned char header = transform_header(message);
                write(data->socket_fd, &header, sizeof(header));
                write(data->socket_fd, &v, 8);
                write(data->socket_fd, message.pay_load, message.pay_load_length);
            } else if(message.header.type_digit == 2){
    //            printf("2\n");
            } else if(message.header.type_digit == 4){
    //            printf("4\n");
            } else if(message.header.type_digit == 6){
    //            printf("6\n");
            } else if(message.header.type_digit == 8){
    //            printf("8\n");
                data->queue->shutdown_flag = 1;
                break;
            } else {
    //            printf("?\n");
                // Send it using exactly the same syscalls as for other file descriptors
                message.header.type_digit = 0xf;
                unsigned char header = transform_header(message);
                uint64_t pay_length = 0;
                write(data->socket_fd, &header, sizeof(header));
                write(data->socket_fd, &pay_length, sizeof(pay_length));
                close(data->socket_fd);
                break;
            }
    //        write(data->socket_fd, message.pay_load, message.pay_load_length);
            free(message.pay_load);
        //    puts(buffer->header);
    //        write(data->socket_fd, data->msg, data->msg_len);
    //        printf("w\n");
            
        }
    }
    close(data->socket_fd);
    free(data);
//    printf("www\n");
    return NULL;
}
void *thread_function(void *arg){
    linked_queue_t *queue = (linked_queue_t *)arg;
    struct connect_data *pclient = NULL;
    while (1) {
        if(queue->shutdown_flag)
            break;
        pthread_mutex_lock(&queue->queue_lock);
        if ((pclient = dequeue(queue))== NULL){
            pthread_cond_wait(&queue->queue_con, &queue->queue_lock);
            pclient = dequeue(queue);
        }
        pthread_mutex_unlock(&queue->queue_lock);
        if (pclient != NULL){
            connection_handler((void*)pclient);
        }
    }
    return NULL;
}


b_file_t* read_binary(char *arg){
    char *file = arg;
    FILE *fp = fopen(file, "rb");
    b_file_t* content = malloc(sizeof(b_file_t));
    fread(&content->ip_v4_address, sizeof(content->ip_v4_address), 1, fp);
    fread(&content->port, sizeof(content->port), 1, fp);
    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp) - 6;
    fseek(fp, 6, SEEK_SET);
    content->message = malloc(size);
//    printf("size %lu\n",size);
    fread(content->message,size, 1, fp);
//    printf("");
    return content;
}

linked_queue_t* initialisze_queue(){
    linked_queue_t *queue = malloc(sizeof(linked_queue_t));
    pthread_mutex_init(&(queue->queue_lock), NULL);
    pthread_cond_init(&queue->queue_con, NULL);
    queue->shutdown_flag = 0;
    queue->head = NULL;
    queue->tail = NULL;
    return queue;
}

void free_queue(linked_queue_t *queue){
    while (queue->head != NULL) {
        struct connect_data *data = dequeue(queue);
        free(data);
    }
    free(queue->msg);
    free(queue);
}


int main(int argc, char** argv){
    b_file_t *file = NULL;
    struct sockaddr_in adds = {0};
    if (argc > 0){
        file = read_binary(argv[1]);
        adds.sin_addr.s_addr = file->ip_v4_address;
        adds.sin_port = file->port;
//        printf("ip_address: %s\n",inet_ntoa(adds.sin_addr));
//        printf("port: %d\n",ntohs(adds.sin_port));
//        printf("file content %s\n",file->message);
    } else{
//        return 0;
    }
    
    int serversocket_fd = -1;
    int clientsocket_fd = -1;
    int port = ntohs(adds.sin_port);
    struct sockaddr_in address;
    int opt = 1;
//    char buffer[1024];
    serversocket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (serversocket_fd < 0){
        perror("failed wrong\n");
        exit(1);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = file->ip_v4_address;
    address.sin_port = htons(port);
    setsockopt(serversocket_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
    if (bind(serversocket_fd, (struct sockaddr*)&address, sizeof(struct sockaddr_in)) < 0){
        perror("borkens wrong\n");
        exit(1);
    }
    if (listen(serversocket_fd, 4) < 0){
        perror("listen wrong\n");
        exit(1);
    }
    linked_queue_t *queue = initialisze_queue();
    pthread_t *pthreads = calloc(SIZE, sizeof(pthread_t));
    queue->msg = file->message;
    for (int i = 0; i < SIZE; i++) {
        pthread_create(&pthreads[i], NULL, thread_function, (void*)queue);
    }
    while (1) {
        if (queue->shutdown_flag)
            break;
        uint32_t addrlen = sizeof(struct sockaddr_in);
        clientsocket_fd = accept(serversocket_fd, (struct sockaddr*)&address, &addrlen);
        struct connect_data *d = malloc(sizeof(struct connect_data));
        d->socket_fd = clientsocket_fd;
        d->queue = queue;
        pthread_mutex_lock(&queue->queue_lock);
        enqueue(queue, d);
        pthread_cond_signal(&queue->queue_con);
        pthread_mutex_unlock(&queue->queue_lock);
//        pthread_t thread;
//        pthread_create(&thread, NULL, connection_handler, d);
    }
    for (size_t i = 0; i < SIZE; i++) {
        if (pthread_join(pthreads[i], NULL) != 0) {
            perror("unable to join thread");
            return 1;
        }
    }
    free_queue(queue);
    free(file);
    close(serversocket_fd);
    return 0;
}
