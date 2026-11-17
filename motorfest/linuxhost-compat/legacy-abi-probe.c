/* SPDX-License-Identifier: AGPL-3.0-only
 * Owned ELF fixture only; never a substitute for the installed vendor client. */
unsigned char __wine_spec_nt_header[512]={[0]='P',[1]='E',[4]=0x64,[5]=0x86,[20]=0xf0,[24]=0x0b,[25]=0x02,[81]=0x02,[132]=16};
static int attached;
__attribute__((ms_abi)) int DllMain(void *module,unsigned reason,void *reserved)
{(void)reserved;if(reason==1){if(!module)return 0;attached=1;}return 1;}
__attribute__((ms_abi)) unsigned GetVer(void){return attached ? 0x12345678:0;}
__attribute__((ms_abi)) double Init(double a,double b,double c,double d,double e,double f)
{unsigned long long fs;__asm__ volatile("rdfsbase %0":"=r"(fs));return attached&&fs ? a+2*b+3*c+4*d+5*e+6*f:-1;}
