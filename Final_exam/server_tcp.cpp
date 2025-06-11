#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>  // For _beginthreadex
#include <direct.h>
#include <errno.h>

#define SERVERPORT 9000
#define BUFSIZE 65536
#define SAVE_DIR "tcp_received\\"
#define MAX_USERNAME 32

#pragma comment(lib, "ws2_32.lib")

typedef struct {
    SOCKET client_sock;
    struct sockaddr_in client_addr;
} client_info_t;

void err_quit(const char *msg) {
    fprintf(stderr, "%s: %d\n", msg, WSAGetLastError());
    exit(1);
}

void create_save_directory() {
    if (_mkdir(SAVE_DIR) == 0) {
        printf("Directory '%s' created successfully.\n", SAVE_DIR);
    } else if (errno != EEXIST) {
        printf("Failed to create directory '%s'\n", SAVE_DIR);
    }
}

void receive_file(SOCKET sock, const char *filename, long long file_size) {
    FILE *file;
    char buf[BUFSIZE];
    int bytes_received;
    long long total_received = 0;

    const char *base_filename = strrchr(filename, '\\');
    if (base_filename) base_filename++;
    else base_filename = filename;

    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s%s", SAVE_DIR, base_filename);

    file = fopen(full_path, "wb");
    if (!file) {
        printf("Error: Cannot create file '%s'\n", full_path);
        send(sock, "ERROR", 5, 0);
        return;
    }

    send(sock, "READY", 5, 0);
    printf("Receiving file: %s (Size: %lld bytes)\n", full_path, file_size);

    while (total_received < file_size) {
        int remaining = (int)(file_size - total_received < BUFSIZE ? file_size - total_received : BUFSIZE);
        bytes_received = recv(sock, buf, remaining, 0);
        if (bytes_received <= 0) break;
        fwrite(buf, 1, bytes_received, file);
        total_received += bytes_received;
        printf("Received %lld/%lld bytes (%.2f%%)\r", total_received, file_size, ((double)total_received / file_size) * 100);
    }

    fclose(file);
    if (total_received == file_size) {
        printf("\nFile received successfully: %s\n", full_path);
        send(sock, "FILE_OK", 7, 0);
    } else {
        printf("\nFile transfer incomplete\n");
        send(sock, "FILE_FAIL", 9, 0);
        remove(full_path);
    }
}

unsigned __stdcall client_handler(void *data) {
    client_info_t *info = (client_info_t *)data;
    SOCKET client_sock = info->client_sock;
    struct sockaddr_in clientaddr = info->client_addr;
    free(info);  // Free the allocated structure

    char addr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientaddr.sin_addr, addr, sizeof(addr));
    
    char username[MAX_USERNAME] = "[unknown]";
    char buf[BUFSIZE];

    // First message should be username
    int retval = recv(client_sock, buf, BUFSIZE - 1, 0);
    if (retval > 0) {
        buf[retval] = '\0';
        if (strncmp(buf, "USER:", 5) == 0) {
            strncpy(username, buf + 5, MAX_USERNAME - 1);
            username[MAX_USERNAME - 1] = '\0';
            printf("Client identified as: %s (%s)\n", username, addr);
            send(client_sock, "USER_OK", 7, 0);
        } else {
            strcpy(username, addr);
            send(client_sock, "USER_FAIL", 9, 0);
        }
    }

    while (1) {
        retval = recv(client_sock, buf, BUFSIZE - 1, 0);
        if (retval <= 0) break;

        buf[retval] = '\0';

        if (strncmp(buf, "FILE:", 5) == 0) {
            char *last_colon = strrchr(buf, ':');
            if (!last_colon) continue;
            
            long long file_size = atoll(last_colon + 1);
            *last_colon = '\0';
            char *filename = buf + 5;
            
            printf("[%s] File transfer: %s (%lld bytes)\n", username, filename, file_size);
            receive_file(client_sock, filename, file_size);
        } else {
            printf("[%s] Message: %s\n", username, buf);
            send(client_sock, buf, retval, 0);
        }
    }

    closesocket(client_sock);
    printf("Client disconnected: %s\n", username);
    return 0;
}

int main() {
    create_save_directory();
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        err_quit("WSAStartup failed");
    }

    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == INVALID_SOCKET) {
        err_quit("Socket creation failed");
    }

    struct sockaddr_in serveraddr = {0};
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(SERVERPORT);

    if (bind(listen_sock, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == SOCKET_ERROR) {
        err_quit("Bind failed");
    }

    if (listen(listen_sock, SOMAXCONN) == SOCKET_ERROR) {
        err_quit("Listen failed");
    }

    printf("Server started on port %d (Multi-client enabled)\n", SERVERPORT);

    while (1) {
        client_info_t *info = (client_info_t *)malloc(sizeof(client_info_t));
        int addrlen = sizeof(info->client_addr);

        info->client_sock = accept(listen_sock, (struct sockaddr *)&info->client_addr, &addrlen);
        if (info->client_sock == INVALID_SOCKET) {
            printf("Accept failed: %d\n", WSAGetLastError());
            free(info);
            continue;
        }

        char addr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &info->client_addr.sin_addr, addr, sizeof(addr));
        printf("New connection from: %s\n", addr);

        // Create a thread for this client
        HANDLE thread = (HANDLE)_beginthreadex(NULL, 0, client_handler, info, 0, NULL);
        if (thread == NULL) {
            printf("Failed to create thread for client\n");
            closesocket(info->client_sock);
            free(info);
        } else {
            CloseHandle(thread); // We don't need to keep track of the thread
        }
    }

    closesocket(listen_sock);
    WSACleanup();
    return 0;
}