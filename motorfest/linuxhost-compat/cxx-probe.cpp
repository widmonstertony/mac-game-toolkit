/* SPDX-License-Identifier: AGPL-3.0-only
 * Real Linux C++ ELF probe, no replacement exception or allocation routines. */
static int cleaned;
static int initialized;
extern "C" int puts(const char *);
__attribute__((constructor)) static void initialize()
{
    initialized=17;
    puts("CXX_CONSTRUCTOR_RAN");
}
struct Cleanup { ~Cleanup() { cleaned++; } };
static __attribute__((noinline)) void throw_value()
{
    Cleanup object;
    throw 42;
}
extern "C" long cxx_test()
{
    if(initialized!=17)return 4;
    volatile char *p=new char[65];
    for (unsigned i=0;i<65;i++)p[i]=(char)i;
    if (p[64]!=64) { delete[] p; return 1; }
    delete[] p;
    try { throw_value(); }
    catch (int value) { return value==42 && cleaned==1 ? 0 : 2; }
    return 3;
}
