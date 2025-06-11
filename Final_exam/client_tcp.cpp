#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define SERVER_IP "127.0.0.2"
#define SERVERPORT 9000
#define BUFSIZE 65536
#define MAX_USERNAME 32

#pragma comment(lib, "ws2_32.lib")

void err_quit(const char *msg) {
    fprintf(stderr, "%s: %d\n", msg, WSAGetLastError());
    exit(1);
}

void send_file(SOCKET sock, const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        printf("Error: Cannot open file '%s'\n", filename);
        return;
    }

    fseek(file, 0, SEEK_END);
    long long file_size = _ftelli64(file);
    fseek(file, 0, SEEK_SET);

    char file_info[BUFSIZE];
    snprintf(file_info, BUFSIZE, "FILE:%s:%lld", filename, file_size);
    send(sock, file_info, strlen(file_info), 0);

    char buf[BUFSIZE];
    int bytes_read, bytes_sent;
    long long total_sent = 0;

    int retval = recv(sock, buf, BUFSIZE - 1, 0); // Wait for server ready
    if (retval <= 0) {
        printf("Server disconnected during file transfer\n");
        fclose(file);
        return;
    }
    buf[retval] = '\0';

    if (strcmp(buf, "READY") != 0) {
        printf("Server not ready: %s\n", buf);
        fclose(file);
        return;
    }

    printf("Sending file: %s (Size: %lld bytes)\n", filename, file_size);
    while ((bytes_read = fread(buf, 1, BUFSIZE, file)) > 0) {
        bytes_sent = send(sock, buf, bytes_read, 0);
        if (bytes_sent == SOCKET_ERROR) break;
        total_sent += bytes_sent;
        printf("Sent %lld/%lld bytes (%.2f%%)\r", total_sent, file_size, ((double)total_sent / file_size) * 100);
    }

    fclose(file);
    
    // Wait for final confirmation from server
    retval = recv(sock, buf, BUFSIZE - 1, 0);
    if (retval > 0) {
        buf[retval] = '\0';
        if (strcmp(buf, "FILE_OK") == 0) {
            printf("\nFile sent successfully\n");
        } else {
            printf("\nFile transfer failed: %s\n", buf);
        }
    } else {
        printf("\nServer disconnected during file transfer confirmation\n");
    }
}

int main() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        err_quit("WSAStartup failed");
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        err_quit("Socket creation failed");
    }

    struct sockaddr_in serveraddr = {0};
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = inet_addr(SERVER_IP);
    serveraddr.sin_port = htons(SERVERPORT);

    if (connect(sock, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == SOCKET_ERROR) {
        err_quit("Connect failed");
    }

    // Get username from user
    char username[MAX_USERNAME];
    printf("Enter your username: ");
    fgets(username, MAX_USERNAME, stdin);
    username[strcspn(username, "\r\n")] = '\0';

    // Send username to server
    char user_msg[BUFSIZE];
    snprintf(user_msg, BUFSIZE, "USER:%s", username);
    if (send(sock, user_msg, strlen(user_msg), 0) == SOCKET_ERROR) {
        err_quit("Username registration failed");
    }

    // Wait for server response
    char buf[BUFSIZE];
    int retval = recv(sock, buf, BUFSIZE - 1, 0);
    if (retval <= 0) {
        err_quit("Connection failed during username registration");
    }
    buf[retval] = '\0';

    if (strcmp(buf, "USER_OK") != 0) {
        printf("Username registration failed. Using default identifier.\n");
    } else {
        printf("Username '%s' registered successfully.\n", username);
    }

    printf("\nConnected to server. Available commands:\n");
    printf("  file <filename> - Send a file\n");
    printf("  quit            - Exit the program\n");
    printf("  Any other text  - Send as message\n\n");

    char cmd[BUFSIZE];
    while (1) {
        printf("%s> ", username);
        fgets(cmd, BUFSIZE, stdin);
        cmd[strcspn(cmd, "\r\n")] = '\0';

        if (strncmp(cmd, "file ", 5) == 0) {
            char *filename = cmd + 5;
            send_file(sock, filename);
        } 
        else if (strcmp(cmd, "quit") == 0) {
            break;
        }
        else if (strlen(cmd) > 0) {
            // Send regular message
            if (send(sock, cmd, strlen(cmd), 0) == SOCKET_ERROR) {
                printf("Error sending message\n");
                break;
            }
            
            // Wait for echo from server
            retval = recv(sock, buf, BUFSIZE - 1, 0);
            if (retval <= 0) break;
            buf[retval] = '\0';
            printf("Server: %s\n", buf);
        }
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}