#include "common.h"
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/wait.h>

// Ignore SIGPIPE so a broken pipe doesn't kill the server process
#include <signal.h>

void server_child_process(int client_fd, int chunk_id, const char* filename, 
                          off_t offset, size_t chunk_size) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) { perror("open file"); exit(EXIT_FAILURE); }
    
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1) {
        perror("lseek");
        close(fd);
        exit(EXIT_FAILURE);
    }

    void *buffer = malloc(chunk_size);
    if (!buffer) { perror("malloc"); close(fd); exit(EXIT_FAILURE); }
    
    ssize_t bytes_read = read(fd, buffer, chunk_size);
    if (bytes_read != (ssize_t)chunk_size) {
        fprintf(stderr, "Read %zd, expected %zu\n", bytes_read, chunk_size);
        free(buffer);
        close(fd);
        exit(EXIT_FAILURE);
    }
    close(fd);

    chunk_header_t header;
    header.seq = htonl(chunk_id);
    header.size = htonl(chunk_size);

    size_t sent = 0;
    while (sent < HEADER_SIZE) {
        ssize_t n = send(client_fd, ((char*)&header) + sent, HEADER_SIZE - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "send header failed (chunk %d): %s\n", chunk_id, strerror(errno));
            free(buffer);
            close(client_fd);
            exit(EXIT_FAILURE);
        } else if (n == 0) {
            fprintf(stderr, "send header returned 0 (chunk %d)\n", chunk_id);
            free(buffer);
            close(client_fd);
            exit(EXIT_FAILURE);
        }
        sent += n;
    }

    fprintf(stderr, "server: sent header for chunk %d size %zu\n", chunk_id, chunk_size);

    sent = 0;
    while (sent < chunk_size) {
        ssize_t n = send(client_fd, ((char*)buffer) + sent, chunk_size - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "send payload failed (chunk %d): %s\n", chunk_id, strerror(errno));
            free(buffer);
            close(client_fd);
            exit(EXIT_FAILURE);
        } else if (n == 0) {
            fprintf(stderr, "send payload returned 0 (chunk %d)\n", chunk_id);
            free(buffer);
            close(client_fd);
            exit(EXIT_FAILURE);
        }
        sent += n;
    }

    fprintf(stderr, "server: finished sending payload for chunk %d\n", chunk_id);

    free(buffer);
    close(client_fd);
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    const char *filename = argv[1];

    // Ignore SIGPIPE so that sending to a closed socket returns EPIPE instead
    signal(SIGPIPE, SIG_IGN);

    struct stat st;
    if (stat(filename, &st) != 0) { perror("stat"); exit(EXIT_FAILURE); }
    size_t total_size = st.st_size;

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr.s_addr = INADDR_ANY
    };
    
    // Error check for bind
    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }
    // Error check for listen
    if (listen(listen_fd, 10) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    // ============================================================
    // Initial handshake: accept first connection, receive N, reply with total size
    // ============================================================
    int *client_fds = NULL;
    int first_fd = -1;
    uint32_t net_N;
    int N;

    while (1) {
        first_fd = accept(listen_fd, NULL, NULL);
        if (first_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept handshake failed");
            exit(EXIT_FAILURE);
        }
        break;
    }

    if (recv(first_fd, &net_N, sizeof(net_N), MSG_WAITALL) != sizeof(net_N)) {
        perror("recv N failed");
        close(first_fd);
        exit(EXIT_FAILURE);
    }

    N = ntohl(net_N);
    if (N <= 0) {
        fprintf(stderr, "Invalid N: %d\n", N);
        close(first_fd);
        exit(EXIT_FAILURE);
    }

    uint32_t net_size = htonl(total_size);
    if (send(first_fd, &net_size, sizeof(net_size), 0) != sizeof(net_size)) {
        perror("send total_size failed");
        close(first_fd);
        exit(EXIT_FAILURE);
    }

    client_fds = malloc(sizeof(int) * N);
    if (!client_fds) {
        perror("malloc client_fds");
        close(first_fd);
        exit(EXIT_FAILURE);
    }

    client_fds[0] = first_fd;
    printf("Server: Transferring %zu bytes using %d chunks\n", total_size, N);

    // ============================================================
    // Accept the remaining N-1 connections and discard the 4-byte total_size header
    // ============================================================
    for (int i = 1; i < N; i++) {
        int fd;
        while (1) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
            if (fd < 0) {
                if (errno == EINTR) continue;
                perror("accept client connection failed");
                for (int j = 0; j < i; j++) close(client_fds[j]);
                free(client_fds);
                exit(EXIT_FAILURE);
            }
            break;
        }

        uint32_t discard_size;
        if (recv(fd, &discard_size, sizeof(discard_size), MSG_WAITALL) != sizeof(discard_size)) {
            perror("recv total_size header failed");
            close(fd);
            for (int j = 0; j < i; j++) close(client_fds[j]);
            free(client_fds);
            exit(EXIT_FAILURE);
        }

        client_fds[i] = fd;
    }

    size_t chunk_size = (total_size + N - 1) / N;

    // ============================================================
    // Fork N children, each processing one client connection
    // ============================================================
    for (int i = 0; i < N; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork failed");
            for (int j = 0; j < N; j++) close(client_fds[j]);
            free(client_fds);
            exit(EXIT_FAILURE);
        }

        if (pid == 0) {
            close(listen_fd);
            off_t offset = (off_t)i * chunk_size;
            size_t actual_size = (i == N - 1) ? (total_size - offset) : chunk_size;
            server_child_process(client_fds[i], i + 1, filename, offset, actual_size);
            exit(EXIT_SUCCESS);
        }

        close(client_fds[i]);
    }

    free(client_fds);

    for (int i = 0; i < N; i++) {
        if (wait(NULL) < 0) {
            perror("wait failed");
            exit(EXIT_FAILURE);
        }
    }

    return 0;
}
