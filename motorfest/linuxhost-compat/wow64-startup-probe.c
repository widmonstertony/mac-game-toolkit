#include <windows.h>
#include <stdio.h>
int main(void) { SYSTEM_INFO s; GetNativeSystemInfo(&s); printf("MOTORFEST_WOW64_PROBE bits=%u native=%u result=PASS\n",(unsigned)(sizeof(void*)*8),s.wProcessorArchitecture); fflush(stdout); return sizeof(void*)==4 ? 0:2; }
