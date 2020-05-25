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
#define SERVER_MSG ("Hello User ! Welcome to my server!\n")
//#include <libkern/OSByteOrder.h>
//#define bswap_16(x) OSSwapInt16(x)
//#define bswap_32(x) OSSwapInt32(x)
//#define bswap_64(x) OSSwapInt64(x)



typedef struct b_file{
    uint32_t ip_v4_address;
    uint16_t port;
    char *message;
}b_file_t;

struct connect_data{
    int socket_fd;
    char *msg;
    size_t msg_len;
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





uint64_t swap_64(uint16_t array[8]){
    uint64_t val = array[0];
    for(int j = 1; j < 8; j++){
        val = val | array[j] << (8*j);
    }
    return val;
}

unsigned char transform_header(message_t message){
    unsigned char header = (message.header.type_digit << 4) | (message.header.compression_bit << 3) | (message.header.require_bit << 2);
    return header;
}


void *connection_handler(void *argv){
    struct connect_data *data = argv;
    while (1) {
//        printf("1\n");
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
            message.header.type_digit = 0x1;
            unsigned char header = transform_header(message);
            write(data->socket_fd, &header, sizeof(header));
            write(data->socket_fd, &v, 8);
            write(data->socket_fd, message.pay_load, message.pay_load_length);
        } else if(message.header.type_digit == 2){
            
        } else if(message.header.type_digit == 4){
        
        } else if(message.header.type_digit == 6){
            
        } else if(message.header.type_digit == 8){
            
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
//    close(data->socket_fd);
    free(data);
//    printf("www\n");
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
    while (1) {
        uint32_t addrlen = sizeof(struct sockaddr_in);
        clientsocket_fd = accept(serversocket_fd, (struct sockaddr*)&address, &addrlen);
        
        struct connect_data *d = malloc(sizeof(struct connect_data));
        d->socket_fd = clientsocket_fd;
        d->msg = SERVER_MSG;
        d->msg_len = strlen(SERVER_MSG) + 1;
        pthread_t thread;
        pthread_create(&thread, NULL, connection_handler, d);
    }
//    close(serversocket_fd);
    return 0;
}
