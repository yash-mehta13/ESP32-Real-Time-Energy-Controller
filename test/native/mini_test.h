#pragma once

// Tiny dependency-free test harness so the host suite builds with a plain
// C++11 compiler (no network, no package manager).
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace mini
{
    struct State { int run; int failed; const char* current; };
    inline State& st() { static State s{0, 0, ""}; return s; }
}

#define TEST(name)                                                         \
    static void name();                                                    \
    static struct name##_reg {                                             \
        name##_reg() { mini_register(#name, name); }                       \
    } name##_instance;                                                     \
    static void name()

typedef void (*mini_fn)();
void mini_register(const char* name, mini_fn fn);

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            mini::st().failed++;                                           \
        }                                                                  \
    } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))
#define CHECK_STR(a, b) CHECK(strcmp((a), (b)) == 0)
