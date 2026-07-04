#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define PORT 9090
#define HEADER_SIZE 8

typedef struct {
    uint32_t seq;    // Sequence number (network byte order)
    uint32_t size;   // Payload size in bytes (network byte order)
} chunk_header_t;

#endif