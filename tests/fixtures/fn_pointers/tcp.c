#include "backend.h"

static int tcp_send(context_t *ctx, int length) {
    (void)ctx;
    return length + 6;
}

const backend_t tcp_backend = {
    .send = tcp_send,
    .header_length = 7,
};
