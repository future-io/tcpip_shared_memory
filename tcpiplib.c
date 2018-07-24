#include <stdio.h>  
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>  
#include <sys/socket.h>   
#include <netinet/in.h>    
#include <netdb.h>  
#include <pthread.h>    
#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/ipc.h>
#include "tcpiplib.h"

int sock;
char already_an_shm;
unsigned char rank;
pthread_t tid;
void* shm;
sem_t send_lock;
sem_t shm_lock;
sem_t other_lock;

int init_server(int portno){
    int sockfd, clilen;
    struct sockaddr_in serv_addr, cli_addr;
    //int n;
    char clientAddr[CLADDR_LEN];
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0){
       printf("ERROR opening socket: %s\n", strerror(errno));
       return -1;
    } 
    printf("Socket created...\n");
    int on = 1;
    int ret = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    if(ret){
        printf("ERROR enabling address reuse: %s", strerror(errno));
    }
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(portno);
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0){
             printf("ERROR on binding: %s\n", strerror(errno));
             return -1;
    } 
    printf("Binding done...\n");
    printf("Waiting for a connection...\n");
    listen(sockfd,5);
    clilen = sizeof(cli_addr);
    sock = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
    if(sock<0){
        printf("ERROR on accept: %s", strerror(errno));
        return -1;
    }
    inet_ntop(AF_INET, &(cli_addr.sin_addr), clientAddr, CLADDR_LEN);
    printf("Connection accepted from %s...\n", clientAddr);


    rank = 0;
    already_an_shm = 0;
    sem_init(&send_lock, 0, 1);
    return sock;
}

int init_client(char* serveraddr, int portno){
    struct sockaddr_in serv_addr; 
    struct hostent *server; 
    printf("Waiting for connection..\n");
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0){
        printf("ERROR opening socket: %s\n", strerror(errno)); 
        return -1;
    } 
    server = gethostbyname(serveraddr); 
    if (server == NULL) { 
        printf("ERROR, no such host\n"); 
        return -1;
    }
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length); 
    serv_addr.sin_port = htons(portno); 
    if (connect(sock,(struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0){
        printf("ERROR connecting"); 
        return -1;
    } 
    else{
        printf("connection established with %s \n", serveraddr);
    }


    rank = 1;
    already_an_shm = 0;
    sem_init(&send_lock, 0, 1);
    return sock;
}


int close_connection(){
    if(already_an_shm){
        destroy_sm();
    }
    sem_destroy(&send_lock);
    return close(sock);
}

int transmit_file(FILE* in){
        
    int sockfd = sock;
    char buffer[BUF_SIZE];
    printf("calculating the size of the file\n");
    fseek(in, 0L, SEEK_END);
    int32_t size = ftell(in);
    rewind(in);
    printf("the size of the file is %d bytes\n", size);
    sem_wait(&send_lock);
    int n = write(sockfd, &size, sizeof(int32_t));
    if (n < 0){
        printf("error writing to socket\n");
        return 1;
    }
    printf("staring to send file\n");
    while(fread(buffer, BUF_SIZE, 1, in)){
        n = write(sockfd, buffer, BUF_SIZE); 
        if (n < 0){ 
            printf("ERROR writing to socket\n"); 
            return 1;
        }
        bzero(buffer, BUF_SIZE);
    }
    sem_post(&send_lock);
    printf("finished sending file\n");
    return 0;
}

int receive_file(char* out){
    int sockfd = sock;
    int ret;
    char buffer[BUF_SIZE];
    memset(buffer, 0, BUF_SIZE);
    FILE* output = fopen(out,"wb");
    if(output == NULL){
        printf("ERROR: cannot open the output file\n");
        return 1;
    }
    int32_t size;
    ret = read(sockfd, &size, sizeof(int32_t)); 
    if(ret <= 0){
        printf("error receiving size of the file\n");
        return 1;
    }
    printf("size of the file is %d bytes\n", size);
    printf("starting to receive file\n");
    //while ((ret = read(sockfd, buffer, BUF_SIZE)) > 0) {
    int i;
    for(i = 0; i < size; i++){
        ret = read(sockfd, buffer, BUF_SIZE);
        if (ret < 0) {
            printf("Error receiving data!\n");
            return 1;
        } 
        fwrite(buffer, BUF_SIZE, 1, output);
        bzero(buffer, BUF_SIZE);
    }
    printf("Closing connection\n");
    fclose(output);
    return 0;
}

int transmit_buffer(void* data, int32_t size) {
    int sockfd = sock;
    char buffer[BUF_SIZE];
    char* data_p = data;
    sem_wait(&send_lock);
    int n = write(sockfd, &size, sizeof(int32_t));
    if (n < 0){
        printf("error writing to socket\n");
        return 1;
    }
    printf("starting to send buffer\n");
    while(size != 0){
        buffer[0] = data_p[size-1]; 
        n = write(sockfd, buffer, BUF_SIZE); 
        if (n < 0){ 
            printf("ERROR writing to socket\n"); 
            return 1;
        }
        bzero(buffer, BUF_SIZE);
        size--;
    }
    sem_post(&send_lock);
    printf("finished sending buffer\n");
    return 0;
}

void* receive_buffer() {//try to see if databuffer can be force to be freed
    int sockfd  = sock;
    int ret;
    char buffer[BUF_SIZE];
    memset(buffer, 0, BUF_SIZE);
    int32_t size = 0;
    ret = read(sockfd, &size, sizeof(int32_t)); 
    if(ret < 0){
        printf("error receiving size of the buffer: %s\n", strerror(errno));
        return NULL;
    }
    printf("size of the buffer is %d bytes\n", size);

    char* databuffer = malloc(size);
    memset(databuffer, 0, size); 
    printf("starting to receive buffer\n");
    //while (()ret = read(sockfd, buffer, BUF_SIZE) > 0) {
    int i;
    for(i = 0; i < size; i++){
        ret = read(sockfd, buffer, BUF_SIZE);
        if (ret < 0) {
            printf("Error receiving data: %s\n", strerror(errno));
            return NULL;
        } 
        databuffer[size - 1 - i] = buffer[0]; 
        bzero(buffer, BUF_SIZE);
    }
    printf("successfully received %d bytes of data\n", i);
    return (void*)databuffer;
}
int transmit_int32(int32_t num){
    char buffer[BUF_SIZE];
    sem_wait(&send_lock);
    int n = write(sock, &num, sizeof(int32_t));
    sem_post(&send_lock);
    if (n < 0){
        printf("error writing to socket\n");
        return 1;
    }
    return 0;
}

int transmit_char(char num){
    char buffer[BUF_SIZE];
    sem_wait(&send_lock);
    int n = write(sock, &num, sizeof(char));
    sem_post(&send_lock);
    if (n < 0){
        printf("error writing to socket\n");
        return 1;
    }
    return 0;
}

void *listener (void *arg){
    int ret;
    char buffer[BUF_SIZE];
    char sync;
    while(1){
        memset(buffer, 0, BUF_SIZE);
        ret = read(sock, &sync, sizeof(char)); 
        if(ret <= 0){
            printf("error receiving sync\n");
            return NULL;
        }
        printf("sync of the file is %d\n", sync);
        if(sync == 0){//write
        }
        else if(sync == 1){//sync
            
        }
        else if(sync == 2){//getOtherLock
            sem_wait(&shm_lock);
            transmit_char((char)3);
        }
        else if(sync == 3){//gotOtherLock
            sem_post(&other_lock);
        } 
        else if(sync == 4){//resize
        }
    }
    return NULL;
}

int init_sm(void* data, int32_t size){
    if(already_an_shm){
        printf("ERROR: There is already an shared memory, only one can be created\n");
        return -1;
    }
    already_an_shm = 1;
    transmit_buffer(data, size);
    if(pthread_create(&tid, NULL, listener, NULL)){
        printf("error creating the listening thread\n");
        return -1;
    }
    sem_init(&shm_lock, 0, 1);
    sem_init(&other_lock, 0 , 0);
    return 0;
}

int accept_sm(){
    shm = receive_buffer();
    if(pthread_create(&tid, NULL, listener, NULL)){
        printf("error creating the listening thread\n");
        return -1;
    }
    already_an_shm = 1;
    sem_init(&shm_lock, 0, 1);
    sem_init(&other_lock, 0, 0);
    return 0;
}

int destroy_sm(){
    sem_destroy(&shm_lock);
    pthread_cancel(tid);
    return 0;
}
//only need to get it's own lock
void* read_sm(int index, int start, int size){
    sem_wait(&shm_lock);
    char* data = malloc(size);
    char* shm_c = (char*) shm;
    int i;
    for(i = 0; i < size; size++){
        data[i] = shm_c[start + i];
    } 
    sem_post(&shm_lock);
    return (void*)data;
}
void getOtherLock(){
    transmit_char((char)2);
    sem_wait(&other_lock);
}
//need to get both locks
int write_sm(void* data, int32_t start, int size){
    if(rank == 0){//server
        sem_wait(&shm_lock);
        getOtherLock();
        //then get the other one
    }
    else{//client
        //first get the other one
        getOtherLock();
        sem_wait(&shm_lock);
    }
    //need to bundle them together 
    transmit_char((char)0);
    transmit_int32(start);
    transmit_buffer(data, size);
    
    if(rank == 0){
        //release the other one first
        sem_post(&shm_lock);
    }
    else{
        sem_post(&shm_lock);
        //release the other one second
    }
}
//need to get both locks
int resize_sm(int new_size){
    
    return 0;
}

int sys_sync(){
    if(!already_an_shm){
        printf("no shared memory in use. can't use sync\n");
        return -1;
    }
    char sync = 1;
    if(rank == 0){//server
        int n = write(sock, &sync, sizeof(sync));
        if (n < 0){
            printf("error writing to socket\n");
            return 1;
        }
    }
    else{
        int sockfd  = sock;
        int ret;
        char buffer[BUF_SIZE];
        memset(buffer, 0, BUF_SIZE);
        int32_t size = 0;
        ret = read(sockfd, &buffer, sizeof(int32_t)); 
        if(ret < 0){
            printf("error receiving sync: %s\n", strerror(errno));
            return -1;
        }
        if(buffer[0] != 1){
            printf("something is not in sync\n");
        }
        int n = write(sockfd, &sync, sizeof(sync));
        if (n < 0){
            printf("error writing to socket\n");
            return 1;
        }

    }
   return 0; 
}
