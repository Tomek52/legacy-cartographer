#include "backend.h"

typedef int (*handler_t)(int value);

int checksum(int value) {
    return value ^ 0x5a;
}

int transfer(context_t *ctx, int length) {
    int sent = ctx->backend->send(ctx, length);
    return sent + ctx->backend->recv(ctx, length);
}

int apply(handler_t handler, int value) {
    return handler(value) + (*handler)(value);
}

int run(context_t *ctx) {
    backend_t local = *ctx->backend;
    return apply(checksum, transfer(ctx, 4)) + local.recv(ctx, 1);
}

int size_only(void) {
    return (int)sizeof(checksum(1));
}

int local_pointer(int value) {
    handler_t local_handler = checksum;
    return local_handler(value);
}
