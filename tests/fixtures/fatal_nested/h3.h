#ifndef FATAL_NESTED_H3
#define FATAL_NESTED_H3

int g(void);

int k(void) {
    struct nope y = g();
    return 0;
}

#endif
