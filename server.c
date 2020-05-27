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
#include <math.h>
#include <byteswap.h>
#include <endian.h>
#define SIZE (20)
#define SERVER_MSG ("compression.dict")
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
    struct compress_dic *com_dict;
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

typedef struct tree_node{
    uint16_t content;
    struct tree_node *left;
    struct tree_node *right;
    int is_external;
}tree_node_t;

typedef struct binary_tree{
    struct tree_node *root;
}binary_tree_t;

typedef struct compress_dic{
    binary_tree_t *tree;
    uint8_t dic[256][256];
    uint8_t len[256];
}compress_dict_t;


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

binary_tree_t *initialize_tree(){
    binary_tree_t *tree = malloc(sizeof(binary_tree_t));
    tree_node_t *node = malloc(sizeof(tree_node_t));
    node->is_external = 0;
    node->left = NULL;
    node->right = NULL;
    return tree;
}

tree_node_t *create_node(int externel){
    tree_node_t *tree_node = malloc(sizeof(tree_node));
    tree_node->is_external = externel;
    tree_node->left = NULL;
    tree_node->right = NULL;
    return tree_node;
}

tree_node_t* insert_node(tree_node_t *root,tree_node_t *node,char number){
    tree_node_t *cur = NULL;
    if (number){
        if (root->right == NULL)
            root->right = node;
        cur = root->right;
    } else{
        if (root->left == NULL)
            root->left = node;
        cur = root->left;
    }
    return cur;
}

compress_dict_t* build_compression(){
    uint8_t length = 0;
    tree_node_t *node = NULL;
    compress_dict_t *compress = malloc(sizeof(compress_dict_t));
    binary_tree_t* tree = initialize_tree();
    FILE* file = fopen(SERVER_MSG, "rb");
//    printf("%d\n",file == NULL);
    fseek(file, 0, SEEK_END);
    unsigned long len_file = ftell(file);
    fseek(file, 0, SEEK_SET);
//    printf("len :%lu\n",len_file);
    uint8_t *dict = malloc(len_file * 8);
    int size = 0;
    for (int i = 0; i < len_file;i++) {
        fread(&length, sizeof(length), 1, file);
        for (int j = 0; j < 8; j++) {
            dict[size++] = (length >> (8 - j - 1)) & 1;
        }
    }
//    printf("size : %d\n",size);
    int switch_read = 1;
    int x = 0;
    int y = 0;
    int i;
    for (i = 0; i < len_file * 8;) {
        if (x == 256){
            break;
        }
        if (switch_read){
            length = 0;
            for (int j = 0; j < 8; j++) {
                length += pow(2, 8 - j - 1) * dict[i++];
            }
            compress->len[x] = length;
            switch_read = 0;
        } else{
            tree_node_t *root = tree->root;
            y = 0;
            for (int j = 0; j < length; j++) {
                compress->dic[x][y] = dict[i++];
                if (j == length - 1){
                    node = create_node(1);
                    node->content = x;
                } else{
                    node = create_node(0);
                }
                root = insert_node(root, node, dict[i++]);
            }
            x++;
            switch_read = 1;
        }
    }
    compress->tree = tree;
    free(dict);
    return compress;
}

void free_tree(tree_node_t *root){
    if (root == NULL)
        return;
    free_tree(root->left);
    free_tree(root->right);
    free(root);
}

void set_bit(char *result,int index,int value){
    int i = index/8;
    int pos = index%8;
    char flag = flag << pos;
    result[i] = result[i] | flag;
}

void *connection_handler(void *argv){
    struct connect_data *data = argv;
    if (!data->queue->shutdown_flag){
        while (1) {
    //        printf("1\n");
//             printf("here\n");
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
                int compress_length = 0;
                char *compression_message = malloc(sizeof(1));
                if (message.header.require_bit == 1){
                    message.header.require_bit = 0;
                    for (int i = 0; i < message.pay_load_length; i++) {
                        char digit_length  = data->queue->com_dict->len[(int)message.pay_load[i]];
                        compression_message = realloc(compression_message, compress_length + digit_length);
                        for (int j = 0; j < digit_length; j++){
                            compression_message[compress_length++] = data->queue->com_dict->dic[i][j];
                        }
                    }
                    if (message.pay_load_length % 8 != 0){
                        int padding = 8 - (message.pay_load_length % 8);
                        for (int i = 0; i < padding; i++){
                            compression_message[compress_length++] = 0;
                        }
                    }
                    char *result = malloc(compress_length / 8);
                    for (int i = 0; i < compress_length; i++){
                        set_bit(result, compress_length - 1, compression_message[compress_length]);
                    }
                    free(message.pay_load);
                    message.pay_load = result;
                    free(compression_message);
                    message.pay_load_length = compress_length/8;
                }
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
    free_tree(queue->com_dict->tree->root);
    free(queue->com_dict->tree);
    free(queue->com_dict);
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
//    queue->com_dict = build_compression();
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


