#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    void *address = dlsym(RTLD_DEFAULT, "dlopen");
    if (!address) return 1;
    printf("0x%llx\n", (unsigned long long)(uintptr_t)address);
    return 0;
}
