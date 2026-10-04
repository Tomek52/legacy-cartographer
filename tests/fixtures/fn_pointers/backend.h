#ifndef FN_POINTERS_BACKEND_H
#define FN_POINTERS_BACKEND_H

typedef struct context context_t;

typedef struct backend {
    int header_length;
    int (*send)(context_t *ctx, int length);
    int (*recv)(context_t *ctx, int length);
} backend_t;

struct context {
    const backend_t *backend;
};

extern const backend_t rtu_backend;

int checksum(int value);

#endif
