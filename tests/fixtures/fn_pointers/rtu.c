#include "backend.h"

static int rtu_send(context_t *ctx, int length) {
    (void)ctx;
    return checksum(length);
}

static int rtu_recv(context_t *ctx, int length) {
    (void)ctx;
    return length;
}

const backend_t rtu_backend = {
    2,
    rtu_send,
    rtu_recv,
};
