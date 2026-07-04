#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif
#include "common.h"
#include <pthread.h>
#include <limits.h>
#include <getopt.h>
#include <time.h>

typedef struct {
    int32_t *data;
    size_t start;
    size_t end;
    pthread_mutex_t *mutex;
    int32_t *global_min;
    int32_t *global_max;
} thread_arg_t;

// ============================================================
// YOUR PART: Min/Max worker thread (Data Parallelism)
// ============================================================
void* min_max_worker(void *arg) {
    thread_arg_t *args = (thread_arg_t*)arg;
    int32_t local_min = INT32_MAX;
    int32_t local_max = INT32_MIN;

    // Each thread scans its assigned block
    for (size_t i = args->start; i < args->end; i++) {
        if (args->data[i] < local_min) local_min = args->data[i];
        if (args->data[i] > local_max) local_max = args->data[i];
    }

    // Reduce with mutex (global min/max)
    pthread_mutex_lock(args->mutex);
    if (local_min < *args->global_min) *args->global_min = local_min;
    if (local_max > *args->global_max) *args->global_max = local_max;
    pthread_mutex_unlock(args->mutex);

    return NULL;
}

// ============================================================
// MEMBER B's TASK: Parallel Sort (TODO)
// ============================================================
#include <time.h> // Added for log timing

// Structure to pass arguments to sorting threads
typedef struct {
    int32_t *data;
    size_t left;
    size_t right;
    int depth;
    int max_threads;
} sort_thread_arg_t;

static void merge(int32_t *data, size_t left, size_t mid, size_t right) {
    size_t n1 = mid - left + 1;
    size_t n2 = right - mid;

    int32_t *L = malloc(n1 * sizeof(int32_t));
    int32_t *R = malloc(n2 * sizeof(int32_t));
    if (!L || !R) {
        perror("malloc merge buffers");
        free(L);
        free(R);
        exit(EXIT_FAILURE);
    }

    for (size_t i = 0; i < n1; i++) L[i] = data[left + i];
    for (size_t j = 0; j < n2; j++) R[j] = data[mid + 1 + j];

    size_t i = 0, j = 0, k = left;
    while (i < n1 && j < n2) {
        if (L[i] <= R[j]) {
            data[k++] = L[i++];
        } else {
            data[k++] = R[j++];
        }
    }
    while (i < n1) data[k++] = L[i++];
    while (j < n2) data[k++] = R[j++];

    free(L);
    free(R);
}

static void* parallel_merge_sort_worker(void *arg) {
    sort_thread_arg_t *args = (sort_thread_arg_t*)arg;
    size_t left = args->left;
    size_t right = args->right;

    if (left >= right) {
        free(args);
        return NULL;
    }

    size_t mid = left + (right - left) / 2;

    if (args->depth < args->max_threads) {
        pthread_t tid;

        sort_thread_arg_t *left_args = malloc(sizeof(sort_thread_arg_t));
        sort_thread_arg_t *right_args = malloc(sizeof(sort_thread_arg_t));
        if (!left_args || !right_args) {
            perror("malloc sort args");
            free(left_args);
            free(right_args);
            free(args);
            exit(EXIT_FAILURE);
        }

        *left_args = (sort_thread_arg_t){args->data, left, mid, args->depth + 1, args->max_threads};
        *right_args = (sort_thread_arg_t){args->data, mid + 1, right, args->depth + 1, args->max_threads};

        if (pthread_create(&tid, NULL, parallel_merge_sort_worker, left_args) != 0) {
            perror("pthread_create");
            free(left_args);
            parallel_merge_sort_worker(right_args); // will free right_args
        } else {
            parallel_merge_sort_worker(right_args); // will free right_args
            if (pthread_join(tid, NULL) != 0) {
                perror("pthread_join");
                // left_args is owned by the joined thread and will be freed there
                free(args);
                exit(EXIT_FAILURE);
            }
            // do NOT free left_args here; the spawned thread frees it
        }
    } else {
        sort_thread_arg_t *left_args = malloc(sizeof(sort_thread_arg_t));
        sort_thread_arg_t *right_args = malloc(sizeof(sort_thread_arg_t));
        if (!left_args || !right_args) {
            perror("malloc serial sort args");
            free(left_args);
            free(right_args);
            free(args);
            exit(EXIT_FAILURE);
        }
        *left_args = (sort_thread_arg_t){args->data, left, mid, args->depth, args->max_threads};
        *right_args = (sort_thread_arg_t){args->data, mid + 1, right, args->depth, args->max_threads};
        parallel_merge_sort_worker(left_args);  // will free left_args
        parallel_merge_sort_worker(right_args); // will free right_args
    }

    merge(args->data, left, mid, right);
    free(args);
    return NULL;
}

static long elapsed_ms(const struct timespec *start, const struct timespec *end) {
    return (end->tv_sec - start->tv_sec) * 1000 + (end->tv_nsec - start->tv_nsec) / 1000000;
}

// ============================================================
// MEMBER B's TASK: Parallel Sort & Part 2 Logging
// ============================================================
void parallel_sort(int32_t *data, size_t num_ints, int T) {
    printf("Starting parallel merge sort with T=%d threads...\n", T);

    struct timespec start, end;
    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
        perror("clock_gettime start");
        return;
    }

    sort_thread_arg_t *root_args = malloc(sizeof(sort_thread_arg_t));
    if (!root_args) {
        perror("malloc root_args");
        return;
    }
    *root_args = (sort_thread_arg_t){data, 0, (num_ints == 0 ? 0 : num_ints - 1), 0, T};
    parallel_merge_sort_worker(root_args);

    if (clock_gettime(CLOCK_MONOTONIC, &end) != 0) {
        perror("clock_gettime end");
    }

    long time_ms = elapsed_ms(&start, &end);

    FILE *out = fopen("result_sorted.dat", "wb");
    if (!out) {
        perror("fopen result_sorted.dat");
    } else {
        size_t written = fwrite(data, sizeof(int32_t), num_ints, out);
        if (written != num_ints) {
            fprintf(stderr, "fwrite result_sorted.dat: wrote %zu of %zu elements\n", written, num_ints);
        }
        if (fclose(out) != 0) {
            perror("fclose result_sorted.dat");
        }
    }

    FILE *log = fopen("execution_log.txt", "a");
    if (!log) {
        perror("fopen execution_log.txt");
    } else {
        if (fprintf(log, "[PART2] THREADS=%d | DATA_PARALLEL=min,max | TASK_PARALLEL=sort\n", T) < 0 ||
            fprintf(log, "[PART2] TIME_MS=%ld | SORT_ALGO=parallel_merge_sort\n", time_ms) < 0 ||
            fprintf(log, "[STATUS] SUCCESS\n") < 0) {
            perror("fprintf execution_log.txt");
        }
        if (fclose(log) != 0) {
            perror("fclose execution_log.txt");
        }
    }
}
// ============================================================
// YOUR PART: operations main - Min/Max computation
// ============================================================
int main(int argc, char *argv[]) {
    int T = 4;
    char *filename = NULL;
    int opt;

    // Parse: ./operations -t <T> -f <file>
    while ((opt = getopt(argc, argv, "t:f:")) != -1) {
        switch(opt) {
            case 't': T = atoi(optarg); break;
            case 'f': filename = optarg; break;
            default:
                fprintf(stderr, "Usage: %s -t <T> -f <file>\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    if (!filename) {
        fprintf(stderr, "Missing -f\n");
        exit(EXIT_FAILURE);
    }

    // Read input file into memory
    FILE *fp = fopen(filename, "rb");
    if (!fp) { perror("fopen input"); exit(EXIT_FAILURE); }
    fseek(fp, 0, SEEK_END);
    size_t file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    int32_t *data = malloc(file_size);
    fread(data, 1, file_size, fp);
    fclose(fp);
    size_t num_ints = file_size / sizeof(int32_t);

    // ============================================================
    // DATA PARALLELISM: Min and Max computation
    // ============================================================
    int32_t global_min = INT32_MAX;
    int32_t global_max = INT32_MIN;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

    pthread_t threads[T];
    thread_arg_t args[T];
    size_t block_size = num_ints / T;

    // Create threads - each handles a block of data
    for (int i = 0; i < T; i++) {
        args[i].data = data;
        args[i].start = i * block_size;
        args[i].end = (i == T - 1) ? num_ints : (i + 1) * block_size;
        args[i].mutex = &mutex;
        args[i].global_min = &global_min;
        args[i].global_max = &global_max;
        pthread_create(&threads[i], NULL, min_max_worker, &args[i]);
    }

    // Wait for all threads
    for (int i = 0; i < T; i++) {
        pthread_join(threads[i], NULL);
    }

    // ============================================================
    // Write output files
    // ============================================================
    FILE *fmin = fopen("result_min.txt", "w");
    fprintf(fmin, "MIN=%d\n", global_min);
    fclose(fmin);

    FILE *fmax = fopen("result_max.txt", "w");
    fprintf(fmax, "MAX=%d\n", global_max);
    fclose(fmax);

    // ============================================================
    // Member B will implement parallel_sort()
    // ============================================================
    parallel_sort(data, num_ints, T);

    free(data);
    return 0;
}