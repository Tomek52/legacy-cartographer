#ifndef MACROS_LOG_H
#define MACROS_LOG_H

int log_value(int value);
int fail(void);

#define LOG(value) log_value(value)
#define CHECKED(expr) ((expr) < 0 ? fail() : (expr))

#endif
