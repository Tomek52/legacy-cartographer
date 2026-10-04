#ifndef TWO_PROGRAMS_LIB_H
#define TWO_PROGRAMS_LIB_H

int work(int value);

inline int twice(int value) {
    return work(value) + work(value);
}

#endif
