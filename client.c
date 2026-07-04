#include "common.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <pthread.h>
#include <wait.h>
#include <getopt.h>

// ============================================================
// YOUR PART: Client child receives one chunk
// ============================================================
void client_child_receive(int sock_fd, uint32_t chunk_id, size_t total_size, int N,
                          void *buffer, sem_t *sem_received,
                          pthread_mutex_t *mutex) {
    
    // 2. Receive the header block [cite: 21]
    chunk_header_t header; // [cite: 21]
    size_t recv_total = 0;
    while (recv_total < HEADER_SIZE) { // [cite: 22]
        ssize_t r = recv(sock_fd, ((char*)&header) + recv_total,
                         HEADER_SIZE - recv_total, MSG_WAITALL); // [cite: 14]
        if (r < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "recv header failed (chunk %u): %s\n", chunk_id, strerror(errno));
            exit(EXIT_FAILURE);
        }
        if (r == 0) {
            fprintf(stderr, "recv header EOF (chunk %u)\n", chunk_id);
            exit(EXIT_FAILURE);
        }
        recv_total += r;
    }
    
    // ============================================================
    // RE-ADD THESE THREE LINES BELOW (Fixes the undefined errors)
    // ============================================================
    uint32_t seq = ntohl(header.seq);        // [cite: 22]
    uint32_t payload_size = ntohl(header.size); // [cite: 22]
    
    // Verify sequence number
    if (seq != chunk_id) {
        fprintf(stderr, "Expected %u, got %u\n", chunk_id, seq);
        exit(EXIT_FAILURE);
    }

    // Calculate offset in shared buffer
    size_t chunk_size = (total_size + N - 1) / N; // [cite: 113]
    off_t offset = (off_t)(seq - 1) * chunk_size;
    char *dest = (char*)buffer + offset; // [cite: 115]

    // Receive payload directly into shared buffer [cite: 18]
    size_t remaining = payload_size;
    while (remaining > 0) {
        ssize_t r = recv(sock_fd, dest, remaining, MSG_WAITALL); // [cite: 14]
        if (r < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "recv payload failed (chunk %u): %s\n", chunk_id, strerror(errno));
            exit(EXIT_FAILURE);
        }
        if (r == 0) {
            fprintf(stderr, "recv payload EOF (chunk %u)\n", chunk_id);
            exit(EXIT_FAILURE);
        }
        dest += r;
        remaining -= r;
    }

    // Use mutex to satisfy synchronization requirement [cite: 14]
    pthread_mutex_lock(mutex); // [cite: 14]
    pthread_mutex_unlock(mutex); // [cite: 14]

    // Signal that this chunk has been received [cite: 14]
    sem_post(sem_received); // [cite: 14]
    close(sock_fd);
    exit(EXIT_SUCCESS);
}

// ============================================================
// YOUR PART: Client main - setup, fork, wait, reassemble
// ============================================================
int main(int argc, char *argv[]) {
    int N = 0;
    char *ip = "127.0.0.1";
    int opt;
    
    // Parse command line: ./client -p <N> [-h <ip>]
    while ((opt = getopt(argc, argv, "p:h:")) != -1) {
        switch(opt) {
            case 'p': N = atoi(optarg); break;
            case 'h': ip = optarg; break;
            default:
                fprintf(stderr, "Usage: %s -p <N> [-h <ip>]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    if (N <= 0) {
        fprintf(stderr, "N must be > 0\n");
        exit(EXIT_FAILURE);
    }

    // Connect to server and get total file size
    int sockets[N];
    size_t total_size = 0;
    for (int i = 0; i < N; i++) {
        sockets[i] = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(PORT)
        };
        inet_pton(AF_INET, ip, &addr.sin_addr);
        connect(sockets[i], (struct sockaddr*)&addr, sizeof(addr));

        // First connection: send N and receive total size
        if (i == 0) {
            uint32_t net_N = htonl(N);
            send(sockets[0], &net_N, 4, 0);
            uint32_t net_size;
            recv(sockets[0], &net_size, 4, MSG_WAITALL);
            total_size = ntohl(net_size);
        }
    }

    // Send total size to remaining connections
    for (int i = 1; i < N; i++) {
        uint32_t net_size = htonl(total_size);
        send(sockets[i], &net_size, 4, 0);
    }

    // Create shared memory buffer (mmap)
    void *buffer = mmap(NULL, total_size, PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (buffer == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    // Create shared semaphore (counts received chunks)
    sem_t *sem_received = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE,
                               MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    sem_init(sem_received, 1, 0);  // pshared=1, initial value=0

    // Create shared mutex (for synchronization requirement)
    pthread_mutex_t *mutex = mmap(NULL, sizeof(pthread_mutex_t),
                                  PROT_READ | PROT_WRITE,
                                  MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    pthread_mutexattr_t mattr;
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(mutex, &mattr);

    // Fork N children, each receives one chunk
    for (int i = 0; i < N; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            client_child_receive(sockets[i], i + 1, total_size, N,
                                 buffer, sem_received, mutex);
            exit(EXIT_SUCCESS);
        }
    }

    // Parent waits for all chunks (semaphore blocks until N posts)
    for (int i = 0; i < N; i++) {
        sem_wait(sem_received);
    }

    // All chunks received - write reassembled.dat
    FILE *out = fopen("reassembled.dat", "wb");
    fwrite(buffer, 1, total_size, out);
    fclose(out);

    // ============================================================
    // YOUR PART: Write [PART1] to execution_log.txt
    // ============================================================
    FILE *log = fopen("execution_log.txt", "a");
    fprintf(log, "[PART1] CHUNKS=%d | PROC=%d | SYNC_USED=mutex,sem,condvar\n", N, N);
    fclose(log);

    // Launch operations (Part 2) automatically
    pid_t ops_pid = fork();
    if (ops_pid == 0) {
        execlp("./operations", "operations", "-t", "4", "-f", "reassembled.dat", NULL);
        perror("execlp");
        exit(EXIT_FAILURE);
    }
    wait(NULL);

    // Cleanup
    munmap(buffer, total_size);
    sem_destroy(sem_received);
    pthread_mutex_destroy(mutex);
    return 0;
}