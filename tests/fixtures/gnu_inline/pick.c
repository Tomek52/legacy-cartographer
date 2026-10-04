int first(void);
int second(void);

extern inline __attribute__((gnu_inline)) int pick(void) { return first(); }
int pick(void) { return second(); }

int first(void) { return 1; }
int second(void) { return 2; }
