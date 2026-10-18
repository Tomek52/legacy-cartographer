struct S {
    int a;
};

int g(struct S s, int t) {
    return s.a + t;
}

int other(void) {
    return 0;
}
