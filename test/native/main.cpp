#include "mini_test.h"

namespace
{
    struct Entry { const char* name; mini_fn fn; };
    Entry entries[512];
    int count = 0;
}

void mini_register(const char* name, mini_fn fn)
{
    entries[count++] = Entry{name, fn};
}

int main()
{
    int failedTests = 0;
    for (int i = 0; i < count; ++i)
    {
        const int before = mini::st().failed;
        entries[i].fn();
        const bool ok = mini::st().failed == before;
        printf("[%s] %s\n", ok ? " OK " : "FAIL", entries[i].name);
        if (!ok) failedTests++;
    }
    printf("\n%d tests, %d failed (host build, simulated/mocked inputs only)\n",
           count, failedTests);
    return failedTests == 0 ? 0 : 1;
}
