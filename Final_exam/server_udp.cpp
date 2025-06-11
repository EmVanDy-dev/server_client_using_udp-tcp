#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <direct.h>
#include <errno.h>

#define SERVERPORT 9000
#define BUFSIZE 65536
#define SAVE_DIR "udp_received\\"

#pragma comment(lib, "ws2_32.lib")

void err_quit(const char *msg) {
    fprintf(stderr, "%s: %d\n", msg, WSAGetLastError());
    exit(1);
}

void create_save_directory() {
    if (_mkdir(SAVE_DIR) == 0) {
        printf("Directory '%s' created successfully.\n", SAVE_DIR);
    } else if (errno == EEXIST) {
        // Directory already exists, no problem
    } else {
        printf("Failed to create directory '%s'\n", SAVE_DIR);
    }
}

void receive_file(SOCKET sock, struct sockaddr_in *clientaddr, int addrlen, const char *filename, long long file_size) {
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s%s", SAVE_DIR, filename);

    FILE *file = fopen(full_path, "wb");
    if (!file) {
        printf("Error: Cannot create file '%s'\n", full_path);
        const char *failmsg = "FILE_FAIL";
        sendto(sock, failmsg, (int)strlen(failmsg), 0, (struct sockaddr *)clientaddr, addrlen);
        return;
    }

    const char *ready_msg = "READY";
    sendto(sock, ready_msg, (int)strlen(ready_msg), 0, (struct sockaddr *)clientaddr, addrlen);

    char buf[BUFSIZE];
    long long total_received = 0;

    while (total_received < file_size) {
        int bytes_to_recv = (int)((file_size - total_received) < BUFSIZE ? (file_size - total_received) : BUFSIZE);
        int bytes_received = recvfrom(sock, buf, bytes_to_recv, 0, NULL, NULL);
        if (bytes_received == SOCKET_ERROR) {
            printf("recvfrom failed: %d\n", WSAGetLastError());
            fclose(file);
            remove(full_path);
            return;
        }
        if (bytes_received == 0) break;

        fwrite(buf, 1, bytes_received, file);
        total_received += bytes_received;

        printf("Received %lld/%lld bytes (%.2f%%)\r", total_received, file_size, (double)total_received / file_size * 100);
    }
    printf("\n");

    fclose(file);

    if (total_received == file_size) {
        printf("File received successfully: %s\n", full_path);
        const char *okmsg = "FILE_OK";
        sendto(sock, okmsg, (int)strlen(okmsg), 0, (struct sockaddr *)clientaddr, addrlen);
    } else {
        printf("File transfer incomplete\n");
        const char *failmsg = "FILE_FAIL";
        sendto(sock, failmsg, (int)strlen(failmsg), 0, (struct sockaddr *)clientaddr, addrlen);
        remove(full_path);
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
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(SERVERPORT);

    if (bind(sock, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == SOCKET_ERROR) {
        err_quit("Bind failed");
    }

    create_save_directory();

    printf("UDP server started on port %d\n", SERVERPORT);

    char buf[BUFSIZE];
    struct sockaddr_in clientaddr;
    int addrlen = sizeof(clientaddr);

    char last_username[64] = "[unknown]";

    while (1) {
        int retval = recvfrom(sock, buf, BUFSIZE - 1, 0, (struct sockaddr *)&clientaddr, &addrlen);
        if (retval == SOCKET_ERROR) {
            printf("recvfrom failed: %d\n", WSAGetLastError());
            continue;
        }
        buf[retval] = '\0';

        // Parse USER message
        if (strncmp(buf, "USER:", 5) == 0) {
            strncpy(last_username, buf + 5, sizeof(last_username) - 1);
            last_username[sizeof(last_username) - 1] = '\0';
            printf("Client identified as: %s\n", last_username);
            const char *ok = "USER_OK";
            sendto(sock, ok, (int)strlen(ok), 0, (struct sockaddr *)&clientaddr, addrlen);
            continue;
        }

        // Parse FILE message
        if (strncmp(buf, "FILE:", 5) == 0) {
            // Format: FILE:<filename>:<filesize>
            char *last_colon = strrchr(buf, ':');
            if (!last_colon) continue;

            long long file_size = atoll(last_colon + 1);
            *last_colon = '\0';
            char *filename = buf + 5;

            printf("[%s] File transfer requested: %s (%lld bytes)\n", last_username, filename, file_size);
            receive_file(sock, &clientaddr, addrlen, filename, file_size);
            continue;
        }

        // Otherwise treat as message
        printf("[%s] says: %s\n", last_username, buf);

        // Echo message back
        sendto(sock, buf, retval, 0, (struct sockaddr *)&clientaddr, addrlen);
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}
