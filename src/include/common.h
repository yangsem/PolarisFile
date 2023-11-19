#ifndef __COMMON_H
#define __COMMON_H

// C++ 头文件
#include <new>
// C 头文件
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>

#if defined(__WIN32) || defined(__WIN64)
#define OS_WIN
#else
#define OS_LINUX
#endif

#define ALIGN8(n) ((n + 7) & ~7)

// 分支优化
#ifdef OS_LINUX
#define likely(x) __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)
#else
#define likely(x) (x)
#define unlikely(x) (x)
#endif

// 终端打印颜色宏
#ifdef OS_LINUX
#define RESET "\033[0m"
#define RED "\033[0;31m"
#define YELLOW "\033[0;33m"
#define BLUE "\033[0;34m"
#define GREEN "\033[0;32m"
#else
#define RESET
#define RED
#define GREEN
#define YELLOW
#define BLUE
#endif

#define TO_STR1(x) #x
#define TO_STR2(x) TO_STR1(x)
#define _POSION_FORMAT "(%s,%s)"
#define __POSION __FUNCTION__, __FILE__ ":" TO_STR2(__LINE__)

#define PRINT_BASE(channel, format, ...)              \
    do                                                \
    {                                                 \
        fprintf(channel, format "\n", ##__VA_ARGS__); \
    } while (0)
#define PRINT_INFO(format, ...) \
    PRINT_BASE(stdout, format _POSION_FORMAT, ##__VA_ARGS__, __POSION);
#define PRINT_WARN(format, ...) \
    PRINT_BASE(stdout, GREEN format _POSION_FORMAT RESET, ##__VA_ARGS__, __POSION);
#define PRINT_ERROR(format, ...) \
    PRINT_BASE(stderr, RED format _POSION_FORMAT RESET, ##__VA_ARGS__, __POSION);
#define PRINT_FAIL(format, ...) \
    PRINT_BASE(stderr, RED format _POSION_FORMAT RESET, ##__VA_ARGS__, __POSION);

#endif //__COMMON_H