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
#include <dirent.h>
#include <sys/stat.h>
#define SIZE (100)
#define ECHO (0x00)
#define DIRECT_LIST (2)
#define FILE_SIZE_QUERY (4)
#define RETRIEVE_FILE (6)
#define SHUTDOWN (8)
#define SERVER_MSG ("compression.dict")
//#include <libkern/OSByteOrder.h>
//#define bswap_16(x) OSSwapInt16(x)
//#define bswap_32(x) OSSwapInt32(x)
//#define bswap_64(x) OSSwapInt64(x)
typedef struct node{
    struct node* next;
    struct connect_data* connect_data;
}node_t;

typedef struct linked_queue{
    struct node *head;
    struct node *tail;
    pthread_mutex_t queue_lock;
    pthread_cond_t queue_con;
    int shutdown_flag;
    char *msg;
    struct compress_dic *com_dict;
    struct session *session;
    struct session *archive;
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
    unsigned char *pay_load;
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
    uint8_t *dict;
    int *len;
}compress_dict_t;

typedef struct session_segment{
    char *file_name;
    uint32_t value;
    uint64_t length;
    uint64_t offset;
}session_segment_t;

typedef struct session{
    session_segment_t *session_ids;
    uint64_t capacity;
    uint64_t size;
    pthread_mutex_t lock;
}session_t;


// for thread pool to enqueue a work
void enqueue(linked_queue_t *queue, struct connect_data* data){
    node_t *newnode = malloc(sizeof(node_t));
    newnode->connect_data = data;
    newnode->next = NULL;
    if (queue->tail == NULL){
        queue->head = newnode;
    } else{
        queue->tail->next = newnode;
    }
    queue->tail = newnode;
}

//change the order of the value
uint64_t swap_uint64(uint64_t val) {
    val = ((val << 8u) & 0xFF00FF00FF00FF00ULL ) | ((val >> 8u) & 0x00FF00FF00FF00FFULL );
    val = ((val << 16u) & 0xFFFF0000FFFF0000ULL ) | ((val >> 16u) & 0x0000FFFF0000FFFFULL );
    return (val << 32u) | (val >> 32u);
}

//for thread pool to dequeue a work
struct connect_data* dequeue(linked_queue_t *queue){
    if (queue->head == NULL){
        return NULL;
    } else{
        struct connect_data *result = queue->head->connect_data;
        node_t *temp = queue->head;
        queue->head = queue->head->next;
        if (queue->head == NULL) {
            queue->tail = NULL;
        }
        free(temp);
        return result;
    }
}

//get the first byte of the header to extract typedigit
unsigned char get_first_digit(unsigned int number){
    return number >> 4;
}
// get the five digit of the header to extract compression bit
unsigned char get_five_digit(unsigned int number){
    number >>= 3;
    return number & 1;
}
//get the six digit of the header to extract require bit
unsigned char get_six_digit(unsigned int number){
    number >>= 2;
    return number & 1;
}

//form the original header
unsigned char transform_header(message_t message){
    unsigned char header = (message.header.type_digit << 4) | (message.header.compression_bit << 3) | (message.header.require_bit << 2);
    return header;
}

//create a node for tree
tree_node_t *create_node(int externel){
    tree_node_t *tree_node = malloc(sizeof(tree_node_t));
    tree_node->is_external = externel;
    tree_node->left = NULL;
    tree_node->right = NULL;
    return tree_node;
}

//initialize a binary tree to store dicitonary list
binary_tree_t *initialize_tree(){
    binary_tree_t *tree = malloc(sizeof(binary_tree_t));
    tree_node_t *node = create_node(0);
    tree->root = node;
    return tree;
}


// insert a node to the binary tree
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

//set a bit to 1 for bit array
void set_bit(uint8_t* array, int index){
    int i = index/8;
    int pos = index%8;

    uint8_t flag = 1;

    flag = flag << (8-pos-1);
    array[i] = array[i] | flag;
}
//get a bit for bit array
uint8_t get_bit(uint8_t* array, int index){
    int i = index/8;
    int pos = index%8;

    uint8_t flag = 1;

    flag = flag << (8-pos-1);

    if ((array[i] & flag) != 0) {
        return 0x1;
    } else {
        return 0x0;
    }
    // return 1 & (array[index / 8] << (index % 8));
}

//get value for a char array
uint64_t parse(uint8_t *array){
    uint64_t value = array[7];
    int pos = 6;
    for (int i = 1; i < 8; i++) {
        value = value | array[i] << (8 * pos);
        pos--;
    }
    return value;
}
//set a bit to 0 for bit array
void clear_bit (uint8_t *array, int index) {
    int i = index/8;
    int pos = index%8;

    unsigned int flag = 1;

    flag = flag << (8-pos-1);
    flag = ~flag;

    array[i] = array[i] & flag;
}
// extract bit from the dicitonay list and build a tree
compress_dict_t* build_compression(){
    tree_node_t *node = NULL;
    compress_dict_t *compress = malloc(sizeof(compress_dict_t));
    binary_tree_t* tree = initialize_tree();
    FILE* file = fopen(SERVER_MSG, "rb");
//    printf("%d\n",file == NULL);
    fseek(file, 0, SEEK_END);
    unsigned long len_file = ftell(file);
    fseek(file, 0, SEEK_SET);
    
//    printf("len :%lu\n",len_file);
    uint8_t *dict_buffer = malloc(len_file);
    fread(dict_buffer, 1, len_file, file);
    fclose(file);
    uint8_t *dict = malloc(sizeof(uint8_t) * 1024);
    dict = memset(dict, 0, sizeof(uint8_t) * 1024);
    int *len = malloc(sizeof(int) * 257);
    int size = 0;
    int i = 0;
    int count = 0;
    // for (int i = 0; i < size*8; i++){
    // while (i < info.st_size*8){
    while (i < len_file * 8){
        uint8_t temp_len = 0;
        for (int j = 0; j < 8 && i < len_file*8; j++){
            if (get_bit(dict_buffer, i++) == 1){
                set_bit(&temp_len, j);
            }
        }
        if (temp_len == 0){
            break;
        }
        len[count] = size;
        tree_node_t* root = tree->root;
        uint8_t number = 0;
        for (int j = 0; j < temp_len; j++){
            // if the bit is the last one, we create an leaf node
            if (j == temp_len - 1){
                node = create_node(1);
                node->content = count;
            }
            //otherwise, we create an internel node
            else{
                node = create_node(0);
            }
            if ((number = get_bit(dict_buffer, i++)) == 1){
                set_bit(dict, size);
            }
            root = insert_node(root, node, number);
            size++;
        }
        count++;
        if (count == 256){
            len[count] = size;
        }

    }
//    printf("size : %d\n",size);
    compress->tree = tree;
    compress->dict = dict;
    compress->len = len;
    return compress;
}
// free a tree
void free_tree(tree_node_t *root){
    if (root == NULL)
        return;
    free_tree(root->left);
    free_tree(root->right);
    free(root);
}

void set_message_bit(char *result,int index,char value){
    int i = index/8;
    int pos = index%8;
    value = value << pos;
    result[i] = result[i] | value;
}
// compressed the payload
void compressed(struct connect_data* data,uint8_t ** compression_message,int c,int *number_bit,int * compress_length){
    int index = data->queue->com_dict->len[c];
    for (int j = index; j < data->queue->com_dict->len[c + 1]; j++) {
        if (*number_bit == *compress_length * 8){
            *compression_message = realloc(*compression_message, ++(*compress_length));
        }
        if (get_bit(data->queue->com_dict->dict, j) == 1){
            set_bit(*compression_message, (*number_bit)++);
        } else{
            clear_bit(*compression_message, (*number_bit)++);
        }
    }
}

// insert session_id to the array
int insert_session_id(session_t* session, uint32_t id,uint64_t offset, uint64_t length,char* file_name){
    int find = 0;
    pthread_mutex_lock(&(session->lock));
    for (int i = 0; i < session->size; i++) {
        if (session->session_ids[i].value == id && session->session_ids[i].offset == offset && session->session_ids[i].length && strcmp(session->session_ids[i].file_name, file_name) == 0) {
            find = 1;
        }
    }
    if(!find){
        if (session->size == session->capacity)
            session->session_ids = realloc(session->session_ids, session->capacity * 2);
        session->session_ids[session->size++].value = id;
        session->session_ids[session->size - 1].file_name = strdup(file_name);
        session->session_ids[session->size - 1].offset = offset;
        session->session_ids[session->size - 1].length = length;
    }
    pthread_mutex_unlock(&(session->lock));
    return find;
}
//return whether a session is in the archive
int find_archive(session_t* session, uint32_t id,uint64_t offset, uint64_t length,char* file_name){
    int find = 0;
    pthread_mutex_lock(&(session->lock));
    for (int i = 0; i < session->size; i++) {
        if (session->session_ids[i].value == id && session->session_ids[i].offset == offset && session->session_ids[i].length && strcmp(session->session_ids[i].file_name, file_name) == 0) {
            find = 1;
            }
    }
    pthread_mutex_unlock(&(session->lock));
    return find;
    

}
//remove a session id is in the array
void remove_session_id(session_t* archive,session_t* session, uint32_t id,uint64_t offset, uint64_t length, char * file_name){
    pthread_mutex_lock(&(session->lock));
    for (int i = 0; i < session->size; i++) {
        if (session->session_ids[i].value == id && session->session_ids[i].offset == offset && session->session_ids[i].length && strcmp(file_name, session->session_ids[i].file_name) == 0) {
            session->session_ids[i].file_name = session->session_ids[session->size - 1].file_name;
            session->session_ids[i].length = session->session_ids[session->size - 1].length;
            session->session_ids[i].value = session->session_ids[session->size - 1].value;
            session->session_ids[i].offset = session->session_ids[session->size - 1].offset;
            if (archive->size == archive->capacity)
                archive->session_ids = realloc(archive->session_ids, archive->capacity * 2);
            archive->session_ids[session->size++].value = id;
            archive->session_ids[session->size - 1].file_name = file_name;
            archive->session_ids[session->size - 1].offset = offset;
            archive->session_ids[session->size - 1].length = length;
            break;
        }
    }
    session->size--;
    pthread_mutex_unlock(&(session->lock));
}
//send error message to server
void send_error_message(struct connect_data *data){
    uint8_t response[9] = {0xf0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0};
    write(data->socket_fd, response, 9);
}
//extract file size and file name
char* extract_file_information(char *file,struct connect_data*data,uint64_t *file_size){
    char *file_name = malloc(strlen(data->queue->msg) + 3 + strlen(file));
    strcpy(file_name, data->queue->msg);
    strcat(file_name, "/");
    strcat(file_name, file);
    struct stat stat_t;
    stat(file_name,&stat_t);
    *file_size = stat_t.st_size;
    return file_name;
}
// check whether a file is in directory
int check_file_in_dir(char* file,struct connect_data* data){
    int number = 0;
    char** files = malloc(1);
    DIR *dir;
    struct dirent* ent;
    if ((dir = opendir(data->queue->msg)) != NULL){
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_type == DT_REG){
                files = realloc(files, (++number) * sizeof(char*));
                files[number - 1] = ent->d_name;
            }
        }
    }
    int found = 0;
    for (int i = 0; i < number; i++) {
        if (strcmp(files[i],file) == 0){
            found = 1;
            break;
        }
    }
    return found;
}
// if file is not in the direcotry, send an error message
char *check(char* file,struct connect_data* data,uint64_t* file_size){
    int found = check_file_in_dir(file, data);
    if (found == 0){
        send_error_message(data);
        return NULL;
    }
    char *file_name = extract_file_information(file, data,file_size);
    return file_name;
}
// send echo message back to client
void send_echo_back(message_t *message,struct connect_data *data,uint64_t v){
    message->header.require_bit = 0;
    unsigned char header = transform_header(*message);
    write(data->socket_fd, &header, sizeof(header));
    write(data->socket_fd, &v, 8);
    write(data->socket_fd, message->pay_load, message->pay_load_length);
}


//echo a meesage to client
void echo_message(message_t* message,struct connect_data* data,uint64_t v){
    if (message->pay_load_length > 0)
        recv(data->socket_fd, message->pay_load, message->pay_load_length, 0);
    message->header.type_digit = 0x1;
    int number_bit = 0;
    int compress_length = 1;
    unsigned char *compression_message = malloc(1);
    if (message->header.require_bit == 1){
        if (message->header.compression_bit == 1){
            send_echo_back(message,data ,v);
        } else{
            for (int i = 0; i < message->pay_load_length; i++) {
                int c = message->pay_load[i];
                compressed(data, &compression_message, c, &number_bit, &compress_length);
            }
            char gap = abs(number_bit - compress_length * 8);
            for (int i = number_bit; i  < compress_length * 8; i++) {
                clear_bit(compression_message, i);
            }
            message->header.compression_bit = 1;
            compression_message = realloc(compression_message, ++compress_length);
            compression_message[compress_length - 1] = gap;
            free(message->pay_load);
            message->pay_load_length = compress_length;
            message->pay_load = compression_message;
            message->header.require_bit = 0;
            unsigned char hexBuffer[100] = {0};
            memcpy((char*)hexBuffer, (char*)&message->pay_load_length,sizeof(int));
            unsigned char header = transform_header(*message);
            write(data->socket_fd, &header, sizeof(header));
            for (int i = 7; i >= 0; i--) {
                send(data->socket_fd,&(hexBuffer[i]),1,0);
            }
            write(data->socket_fd, message->pay_load, message->pay_load_length);
        }
    } else{
        send_echo_back(message,data ,v);
    }
}

void direct_list(message_t* message,struct connect_data* data){
    if (message->pay_load_length > 0)
        recv(data->socket_fd, message->pay_load, message->pay_load_length, 0);
    //                printf("2\n");
    DIR* dir;
    struct dirent* ent;
    int pay_load_length = 0;
    unsigned char *respone = calloc(100, sizeof(char));
    //                printf("msg:    is     %s\n",data->queue->msg);
    if (message->header.compression_bit == 0){
        if ((dir = opendir(data->queue->msg)) != NULL){
            while ((ent = readdir(dir)) != NULL) {
                if (ent->d_type == 8) {
                    for (int k = 0; k < strlen(ent->d_name); k++) {
                        respone[pay_load_length + k] = ent->d_name[k];
                    }
                    pay_load_length += (strlen(ent->d_name) + 1);
                    respone[pay_load_length - 1] = 0x00;
                }
            }
            closedir(dir);
            respone[pay_load_length - 1] = 0x00;
            respone = realloc(respone, pay_load_length);
            if (message->header.require_bit == 0){
                uint8_t response_header = 0x30;
                send(data->socket_fd, &response_header, 1, 0);
                message->pay_load_length = pay_load_length;
                message->pay_load = respone;
            } else{
                int number_bit = 0;
                int compress_length = 1;
                unsigned char *compression_message = malloc(1);
                for (int i = 0; i < pay_load_length; i++){
                    int c = respone[i];
                        compressed(data, &compression_message, c, &number_bit, &compress_length);
                    }
                char gap = abs(number_bit - compress_length * 8);
                for (int i = number_bit; i  < compress_length * 8; i++) {
                    clear_bit(compression_message, i);
                }
                compression_message = realloc(compression_message, ++compress_length);
                compression_message[compress_length - 1] = gap;
                message->pay_load_length = compress_length;
                message->pay_load = compression_message;
                message->header.type_digit = 0x3;
                message->header.compression_bit = 1;
                message->header.require_bit = 0;
                unsigned char header = transform_header(*message);
                write(data->socket_fd, &header, sizeof(header));
            }
            unsigned char hexBuffer[100] = {0};
            memcpy((char*)hexBuffer, (char*)&message->pay_load_length,sizeof(int));
            for (int i = 7; i >= 0; i--) {
                write(data->socket_fd,&(hexBuffer[i]),1);
            }
            write(data->socket_fd, message->pay_load, message->pay_load_length);
        }
    }
}
//extra file size
void file_size_query(message_t* message, struct connect_data*data){
    if (message->pay_load_length > 0)
        recv(data->socket_fd, message->pay_load, message->pay_load_length, 0);
    //                printf("4\n");
    char *file = (char*)message->pay_load;
    int found = check_file_in_dir(file, data);
    if (found == 0){
        send_error_message(data);
    } else{
        uint64_t file_size = 0;
        char *file_name = extract_file_information(file, data,&file_size);
        if (message->header.require_bit == 0){
            // if no need to require compression message
            uint8_t header = {0x50};
            write(data->socket_fd, &header, sizeof(header));
            uint8_t pay_load_len[9] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08};
            write(data->socket_fd, &pay_load_len, 8);
            unsigned char* pay_load = (unsigned char*)&file_size;
            for (int i = 7; i >= 0; i--) {
                write(data->socket_fd, &(pay_load[i]), 1);
            }
        } else{
            // it is need to compress message
            message->header.type_digit = 0x5;
            message->header.compression_bit = 1;
            message->header.require_bit = 0;
            unsigned char header = transform_header(*message);
            write(data->socket_fd, &header, sizeof(header));
            int number_bit = 0;
            int compress_length = 1;
            unsigned char *compression_message = malloc(1);
            file_size = swap_uint64(file_size);
            unsigned char* pay_load = (unsigned char*)&file_size;
            for (int i  = 0; i  < 8; i++) {
                int c = pay_load[i];
                compressed(data, &compression_message, c, &number_bit, &compress_length);
            }
            char gap = abs(number_bit - compress_length * 8);
            for (int i = number_bit; i  < compress_length * 8; i++) {
                clear_bit(compression_message, i);
            }
            compression_message = realloc(compression_message, ++compress_length);
            compression_message[compress_length - 1] = gap;
            message->pay_load_length = compress_length;
            unsigned char hexBuffer[100] = {0};
            memcpy((char*)hexBuffer, (char*)&message->pay_load_length,sizeof(int));
            message->pay_load = compression_message;
            for (int i = 7; i >= 0; i--) {
                send(data->socket_fd,&(hexBuffer[i]),1,0);
            }
            write(data->socket_fd, message->pay_load, message->pay_load_length);
        }
        free(file_name);
    }
}

void retrieve_file(message_t* message,struct connect_data* data){
    //if no need to decompressed
    if (message->header.compression_bit == 0){
        char *file_name;
        uint64_t offset = -1;;
        uint64_t old_offset;
        uint32_t session_id;
        uint64_t offset_length;
        uint64_t old_offset_length = -1;
        if (message->pay_load_length > 0){
            uint64_t file_len = message->pay_load_length - 20;
            file_name  = malloc(file_len);
            read(data->socket_fd, &session_id, 4);
            read(data->socket_fd, &offset, 8);
            old_offset = offset;
            offset = swap_uint64(offset);
            read(data->socket_fd, &offset_length, 8);
            old_offset_length = offset_length;
            offset_length = swap_uint64(offset_length);
            read(data->socket_fd, file_name, file_len);
        }
        uint64_t file_size = 0;
        char* file = check(file_name, data, &file_size);
        if (file != NULL){
            uint64_t range = offset + offset_length;
            if (range > file_size){
                // range is larger than file size, send back error message
                send_error_message(data);
            } else {
                FILE *fp = fopen(file, "r");
                fseek(fp, offset, SEEK_SET);
                unsigned char* file_content = malloc(offset_length);
                fread(file_content, offset_length, 1, fp);
                if (message->header.require_bit == 0) {
                    if (!find_archive(data->queue->archive, session_id, offset, offset_length, file_name)){
                        int multiplex = insert_session_id(data->queue->session, session_id, offset, offset_length,file_name);
                        if (!multiplex){
                            // if session id has exist in the array, then send back an type digit is enough
                            unsigned char header = {0x70};
                            write(data->socket_fd, &header, 1);
                            uint64_t pay_length = offset_length + 20;
                            unsigned char*result = (unsigned char *)&pay_length;
                            for(int i = 7;  i >= 0; i--){
                                write(data->socket_fd, &result[i], 1);
                        }
                        write(data->socket_fd, &session_id , 4);
                        write(data->socket_fd, &old_offset, 8);
                        write(data->socket_fd, &old_offset_length, 8);
                        write(data->socket_fd, file_content, offset_length);
                        fclose(fp);
                        remove_session_id(data->queue->archive,data->queue->session, session_id, offset, offset_length, file_name);
                    } else{
    //                                        printf("should not in1\n");
                        unsigned char header = {0x70};
                        write(data->socket_fd, &header, 1);
                        uint64_t pay_length = 0;
                        unsigned char*result = (unsigned char *)&pay_length;
                        for(int i = 7;  i >= 0; i--){
                            write(data->socket_fd, &result[i], 1);
                            }
                        }
                    } else {
    //                                    printf("should not in2\n");
                        send_error_message(data);
                    }
                } else{
                    int number_bit = 0;
                    int compress_length = 1;
                    unsigned char *compression_message = malloc(1);
                    unsigned char* session = (unsigned char *)&session_id;
                    for (int i = 0; i < 4; i++) {
                        int c = session[i];
                        compressed(data, &compression_message, c, &number_bit, &compress_length);
                    }
                    unsigned char* compress_offset = (unsigned char*)&old_offset;
                    for (int i = 0; i < 8; i++) {
                        int c = compress_offset[i];
                        compressed(data, &compression_message, c, &number_bit, &compress_length);
                    }
                    unsigned char* compress_offset_length = (unsigned char*)&old_offset_length;
                    for (int i = 0; i < 8; i++) {
                        int c = compress_offset_length[i];
                        compressed(data, &compression_message, c, &number_bit, &compress_length);
                    }
                    for (int i = 0; i < offset_length; i++) {
                        int c = file_content[i];
                        compressed(data, &compression_message, c, &number_bit, &compress_length);
                    }
                    char gap = abs(number_bit - compress_length * 8);
                    for (int i = number_bit; i  < compress_length * 8; i++) {
                        clear_bit(compression_message, i);
                    }
                    compression_message = realloc(compression_message, ++compress_length);
                    compression_message[compress_length - 1] = gap;
                    message->header.type_digit = 0x7;
                    message->header.compression_bit = 1;
                    message->header.require_bit = 0;
                    unsigned char header = transform_header(*message);
                    write(data->socket_fd, &header, sizeof(header));
                    message->pay_load_length = compress_length;
                    unsigned char hexBuffer[100] = {0};
                    memcpy((char*)hexBuffer, (char*)&message->pay_load_length,sizeof(int));
                    message->pay_load = compression_message;
                    for (int i = 7; i >= 0; i--) {
                        send(data->socket_fd,&(hexBuffer[i]),1,0);
                    }
                        write(data->socket_fd, message->pay_load, message->pay_load_length);
                }
            }
            free(file);
            free(file_name);
        }
        } else{
            // if is need to decompress
            if (message->pay_load_length > 0)
                recv(data->socket_fd, message->pay_load, message->pay_load_length, 0);
            // receive whole pay load instead of receive separately
                char *decompression_array = malloc(1);
                int size = 0;
                uint64_t length = (message->pay_load[message->pay_load_length - 1]);
    //                    printf("length is what %lu\n",(long)length);
                tree_node_t *root = data->queue->com_dict->tree->root;
                long gap = (message->pay_load_length - 1) * 8 - length;
                for (int i = 0; i < gap; i++) {
                    if (get_bit(message->pay_load, i) == 1){
                        root = root->right;
                        if (root->is_external == 1){
                            decompression_array = realloc(decompression_array, ++size);
                            decompression_array[size - 1] = root->content;
                            root = data->queue->com_dict->tree->root;
                        }
                    } else{
                        root = root->left;
                        if (root->is_external == 1){
                            decompression_array = realloc(decompression_array, ++size);
                            decompression_array[size - 1] = root->content;
                            root = data->queue->com_dict->tree->root;
                            }
                        }
                    }
                    uint32_t session_id = 0;
                    char * target_file = strdup(decompression_array + 20);
    //                    printf("%s\n",target_file);
                    memcpy(&session_id, decompression_array, 4);
                    uint64_t offset = parse((unsigned char*)&decompression_array[4]);
                    uint64_t offset_length = parse((unsigned char*)&decompression_array[12]);
                    uint64_t file_size = 0;
                    char *file_path = check(target_file, data, &file_size);
                    if (file_path != NULL){
                        uint64_t range = offset + offset_length;
                        if (range > file_size){
                            send_error_message(data);
                        } else{
                            FILE *fp = fopen(file_path, "r");
                            fseek(fp, offset, SEEK_SET);
                            unsigned char* file_content = malloc(offset_length);
                            fread(file_content, offset_length, 1, fp);
                            if (message->header.require_bit == 0){
                                uint8_t header = {0x70};
                                write(data->socket_fd, &header, 1);
                                uint64_t pay_load_length = offset_length + 20;
                                unsigned char* result = (unsigned char *)&pay_load_length;
                                for (int i = 7; i >= 0; i--) {
                                    write(data->socket_fd, &result[i], 1);
                                }
                                uint64_t new_offset = swap_uint64(offset);
                                uint64_t new_offset_length = swap_uint64(offset_length);
                                write(data->socket_fd, &session_id, 4);
                                write(data->socket_fd, &(new_offset), 8);
                                write(data->socket_fd, &(new_offset_length), 8);
                                write(data->socket_fd, file_content, offset_length);
                            } else{
                                unsigned char* session = (unsigned char*)&session_id;
                                int number_bit = 0;
                                int compress_length = 1;
                                unsigned char *compression_message = malloc(1);
                                uint64_t new_offset = swap_uint64(offset);
                                uint64_t new_offset_length = swap_uint64(offset_length);
                                for (int i = 0; i < 4; i++) {
                                    int c = session[i];
                                    compressed(data, &compression_message, c, &number_bit, &compress_length);
                                }
                                unsigned char* compress_offset = (unsigned char*)&new_offset;
                                for (int i = 0; i < 8; i++) {
                                    int c = compress_offset[i];
                                    compressed(data, &compression_message, c, &number_bit, &compress_length);
                                }
                                unsigned char* compress_offset_length = (unsigned char*)&new_offset_length;
                                for (int i = 0; i < 8; i++) {
                                    int c = compress_offset_length[i];
                                    compressed(data, &compression_message, c, &number_bit, &compress_length);
                                }
                                for (int i = 0; i < offset_length; i++) {
                                    int c = file_content[i];
                                    compressed(data, &compression_message, c, &number_bit, &compress_length);
                                }
                                char gap = abs(number_bit - compress_length * 8);
                                for (int i = number_bit; i  < compress_length * 8; i++) {
                                    clear_bit(compression_message, i);
                                }
                                compression_message = realloc(compression_message, ++compress_length);
                                compression_message[compress_length - 1] = gap;
                                message->header.type_digit = 0x7;
                                message->header.compression_bit = 1;
                                message->header.require_bit = 0;
                                unsigned char header = transform_header(*message);
                                write(data->socket_fd, &header, sizeof(header));
                                message->pay_load_length = compress_length;
                                unsigned char hexBuffer[100] = {0};
                                memcpy((char*)hexBuffer, (char*)&message->pay_load_length,sizeof(int));
                                message->pay_load = compression_message;
                                for (int i = 7; i >= 0; i--) {
                                    send(data->socket_fd,&(hexBuffer[i]),1,0);
                                }
                                write(data->socket_fd, message->pay_load, message->pay_load_length);
                            }
                        }
                        free(file_path);
                    }
    //                    char * file = malloc();
                }
    //                printf("6\n");
                    
                    
}

void *connection_handler(void *argv){
    struct connect_data *data = argv;
    if (!data->queue->shutdown_flag){
        while (1) {
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
            if(message.header.type_digit == ECHO){
                echo_message(&message, data, v);
            } else if(message.header.type_digit == DIRECT_LIST){
                direct_list(&message, data);
            } else if(message.header.type_digit == FILE_SIZE_QUERY){
                file_size_query(&message, data);
            } else if(message.header.type_digit == RETRIEVE_FILE){
                retrieve_file(&message, data);
            } else if(message.header.type_digit == SHUTDOWN){
                if (message.pay_load_length > 0)
                    recv(data->socket_fd, message.pay_load, message.pay_load_length, 0);
//                printf("8\n");
                data->queue->shutdown_flag = 1;
                break;
            } else {
                if (message.pay_load_length > 0)
                    recv(data->socket_fd, message.pay_load, message.pay_load_length, 0);
                send_error_message(data);
                close(data->socket_fd);
                break;
            }
    //        write(data->socket_fd, message.pay_load, message.pay_load_length);
            free(message.pay_load);
            
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
    content->message = malloc(size + 1);
//    printf("size %lu\n",size);
    fread(content->message,size, 1, fp);
    content->message[size] = '\0';
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
    free(queue->session->session_ids);
    pthread_mutex_destroy(&(queue->session->lock));
    free(queue->session);
    free(queue->archive->session_ids);
    pthread_mutex_destroy(&(queue->archive->lock));
    free(queue->archive);
    free(queue->com_dict->len);
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
    // create socket
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
    if (listen(serversocket_fd, 1000) < 0){
        perror("listen wrong\n");
        exit(1);
    }
    linked_queue_t *queue = initialisze_queue();
    pthread_t *pthreads = calloc(SIZE, sizeof(pthread_t));
    queue->msg = file->message;
    for (int i = 0; i < SIZE; i++) {
        pthread_create(&pthreads[i], NULL, thread_function, (void*)queue);
    }
    queue->com_dict = build_compression();
    session_t *session = malloc(sizeof(session_t));
    session->capacity = 20;
    session->size = 0;
    session->session_ids = calloc(20, sizeof(session_segment_t));
    pthread_mutex_init(&(session->lock), NULL);
    queue->session = session;
    session_t *archive = malloc(sizeof(session_t));
    archive->capacity = 20;
    archive->size = 0;
    archive->session_ids = calloc(20, sizeof(session_segment_t));
    pthread_mutex_init(&(archive->lock), NULL);
    queue->archive = archive;
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
