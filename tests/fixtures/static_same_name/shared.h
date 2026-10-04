#ifndef SHARED_H
#define SHARED_H

int left_value(void);
int right_value(void);
int library_only(int code, ...);
static int callback(void);
static inline int twice(int value) { return 2 * value; }

#endif
