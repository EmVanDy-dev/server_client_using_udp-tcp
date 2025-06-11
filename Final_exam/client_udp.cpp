#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define SERVER_IP "127.0.0.1"
#define SERVERPORT 9000
#define BUFSIZE 65536
#define MAX_USERNAME 32

#pragma comment(lib, "ws2_32.lib")

void err_quit(const char *msg) {
    fprintf(stderr, "%s: %d\n", msg, WSAGetLastError());
    exit(1);
}

const char *basename(const char *path) {
    const char *slash = strrchr(path, '\\');
    if (!slash) slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

void send_file(SOCKET sock, struct sockaddr_in *serveraddr, int addrlen, const char *filepath) {
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        printf("Error: Cannot open file '%s'\n", filepath);
        return;
    }

    fseek(file, 0, SEEK_END);
    long long file_size = _ftelli64(file);
    fseek(file, 0, SEEK_SET);

    char file_only[260];
    strncpy(file_only, basename(filepath), sizeof(file_only));
    file_only[sizeof(file_only) - 1] = '\0';

    char file_info[BUFSIZE];
    snprintf(file_info, BUFSIZE, "FILE:%s:%lld", file_only, file_size);

    // Send file info
    if (sendto(sock, file_info, (int)strlen(file_info), 0, (struct sockaddr *)serveraddr, addrlen) == SOCKET_ERROR) {
        printf("Failed to send file info\n");
        fclose(file);
        return;
    }

    char buf[BUFSIZE];
    int retval;

    // Wait for server READY
    retval = recvfrom(sock, buf, BUFSIZE - 1, 0, NULL, NULL);
    if (retval == SOCKET_ERROR) {
        printf("Server not responding.\n");
        fclose(file);
        return;
    }
    buf[retval] = '\0';

    if (strcmp(buf, "READY") != 0) {
        printf("Server not ready: %s\n", buf);
        fclose(file);
        return;
    }

    printf("Sending file: %s (%lld bytes)\n", file_only, file_size);

    long long total_sent = 0;
    while (total_sent < file_size) {
        int bytes_to_send = (int)((file_size - total_sent) < BUFSIZE ? (file_size - total_sent) : BUFSIZE);
        int bytes_read = (int)fread(buf, 1, bytes_to_send, file);
        if (bytes_read <= 0) break;

        int sent = sendto(sock, buf, bytes_read, 0, (struct sockaddr *)serveraddr, addrlen);
        if (sent == SOCKET_ERROR) {
            printf("sendto failed: %d\n", WSAGetLastError());
            fclose(file);
            return;
        }

        total_sent += sent;
        printf("Sent %lld/%lld bytes (%.2f%%)\r", total_sent, file_size, (double)total_sent / file_size * 100);
    }
    printf("\n");

    fclose(file);

    // Wait for server confirmation
    retval = recvfrom(sock, buf, BUFSIZE - 1, 0, NULL, NULL);
    if (retval <= 0) {
        printf("Server disconnected during file transfer confirmation\n");
        return;
    }
    buf[retval] = '\0';

    if (strcmp(buf, "FILE_OK") == 0) {
        printf("File sent successfully\n");
    } else {
        printf("File transfer failed: %s\n", buf);
    }
}

int main() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        err_quit("WSAStartup failed");
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        err_quit("Socket creation failed");
    }

    struct sockaddr_in serveraddr = {0};
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(SERVERPORT);

    if (inet_pton(AF_INET, SERVER_IP, &serveraddr.sin_addr) <= 0) {
        err_quit("Invalid server IP");
    }

    char username[MAX_USERNAME];
    printf("Enter your username: ");
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\r\n")] = 0;

    // Send username to server
    char user_msg[BUFSIZE];
    snprintf(user_msg, sizeof(user_msg), "USER:%s", username);
    sendto(sock, user_msg, (int)strlen(user_msg), 0, (struct sockaddr *)&serveraddr, sizeof(serveraddr));

    // Wait for server response
    char buf[BUFSIZE];
    int retval = recvfrom(sock, buf, BUFSIZE - 1, 0, NULL, NULL);
    if (retval <= 0) {
        err_quit("Connection failed during username registration");
    }
    buf[retval] = '\0';

    if (strcmp(buf, "USER_OK") != 0) {
        printf("Username registration failed. Using default identifier.\n");
    } else {
        printf("Username '%s' registered successfully.\n", username);
    }

    printf("\nCommands:\n");
    printf("  file <filepath> - Send file\n");
    printf("  quit            - Exit\n");
    printf("  Any other text  - Send message\n\n");

    while (1) {
        printf("%s> ", username);
        if (!fgets(buf, sizeof(buf), stdin)) break;
        buf[strcspn(buf, "\r\n")] = 0;

        if (strncmp(buf, "file ", 5) == 0) {
            send_file(sock, &serveraddr, sizeof(serveraddr), buf + 5);
        } else if (strcmp(buf, "quit") == 0) {
            break;
        } else if (strlen(buf) > 0) {
            // Send message
            if (sendto(sock, buf, (int)strlen(buf), 0, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == SOCKET_ERROR) {
                printf("Error sending message\n");
                break;
            }

            // Wait for echo reply
            retval = recvfrom(sock, buf, BUFSIZE - 1, 0, NULL, NULL);
            if (retval <= 0) break;
            buf[retval] = '\0';
            printf("Server: %s\n", buf);
        }
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}
