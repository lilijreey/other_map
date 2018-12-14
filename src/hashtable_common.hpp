//
// Created by god on 12/13/18.
//

#ifndef HASHTABLE_HASHTABLE_COMMON_HPP
#define HASHTABLE_HASHTABLE_COMMON_HPP

#include <stdint.h>
#include <cmath>

//判断一个数N是否为素数,
//判断 这个数能否被，[2 ~ ceil(sqrt(n))] 之间的数能否整除
static inline bool is_prime_num(size_t n) {
    if (n <= 1) return false;

    const size_t max = (size_t)sqrt(n);

    for (size_t i = 2; i <= max; ++i) {
        if ((n % i) == 0)  //能够整除
            return false;
    }

    return true;
}

//得到小于n的最大素数
static inline size_t find_perv_prime(size_t n) {
    for (--n; n > 0; --n) {
        if (is_prime_num(n)) return n;
    }

    return 0;
}

#endif //HASHTABLE_HASHTABLE_COMMON_HPP
