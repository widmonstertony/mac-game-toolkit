/* SPDX-License-Identifier: AGPL-3.0-only */
#include <windows.h>
#include <stdlib.h>
int main(int argc,char **argv){Sleep(argc>1 ? (DWORD)strtoul(argv[1],0,10):300);return 7;}
