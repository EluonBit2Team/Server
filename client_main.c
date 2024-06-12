#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_IP "192.168.0.253"
//#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 3335
#define MAX_MESSAGE_SIZE 2048

void *receive_thread(void *arg) {
    int sockfd = *((int *)arg);
    char buffer[MAX_MESSAGE_SIZE];

    while (1) {
        int bytes_received = recv(sockfd, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0) {
            printf("Server disconnected.\n");
            break;
        }

        buffer[bytes_received] = '\0';
        printf("Received from server: %s\n", buffer);
    }

    close(sockfd);
    pthread_exit(NULL);
}

int main() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("connect");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    pthread_t tid;
    if (pthread_create(&tid, NULL, receive_thread, (void *)&sockfd) != 0) {
        perror("pthread_create");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    char message[MAX_MESSAGE_SIZE];
    memset(message, '\0', MAX_MESSAGE_SIZE);
    while (1) {
        printf("Enter message to send (or type 'exit' to quit): ");
        fgets(message + sizeof(int), MAX_MESSAGE_SIZE - sizeof(int), stdin);
        message[strcspn(message + sizeof(int), "\n") + sizeof(int)] = '\0'; // Remove newline character
        int input_len = strlen(message + sizeof(int)) + sizeof(int);

        printf("meesage size: %d\n", input_len);
        memcpy(message, &input_len, sizeof(input_len));
        
        if (strcmp(message + sizeof(int), "exit") == 0) {
            printf("Exiting...\n");
            break;
        }

        int bytes_sent = send(sockfd, message, input_len, 0);
        if (bytes_sent == -1) {
            perror("send");
            break;
        }
    }

    pthread_join(tid, NULL);
    close(sockfd);
    return 0;
}