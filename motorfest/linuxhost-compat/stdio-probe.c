/* SPDX-License-Identifier: AGPL-3.0-only
 * Actual Linux ELF callers with Linux pointer sizes and scalar layouts. */
typedef unsigned long size_t;
extern int sscanf(const char*,const char*,...);
extern int __fprintf_chk(void*,int,const char*,...);
extern int strcmp(const char*,const char*),close(int),fclose(void*),ferror(void*);
extern int *__errno_location(void);
extern void *fdopen(int,const char*);
extern long syscall(long,...),lseek(int,long,int);
extern size_t fread(void*,size_t,size_t,void*);
#define CHECK(x) do{if(!(x))return __LINE__;}while(0)
long compat_test(void)
{
    long large=0,neg=0;size_t sz=0;unsigned long hex=0;
    CHECK(sscanf("4294967296 -9223372036854775807 18446744073709551615 ffffffffffffffff","%ld %ld %zu %lx",&large,&neg,&sz,&hex)==4);
    CHECK(large==4294967296L && neg==-9223372036854775807L && sz==~0UL && hex==~0UL);
    signed char small=0;short medium=0;long count=-1;char word[8]={0};int number=0;
    CHECK(sscanf("-12 32000 abc 17","%hhd %hd %3[a-z] %d%ln",&small,&medium,word,&number,&count)==4);
    CHECK(small==-12 && medium==32000 && !strcmp(word,"abc") && number==17 && count==16);
    float f=0;double d=0;long double ld=0;
    CHECK(sscanf("1.25 -3.5 1.03125","%f %lf %Lf",&f,&d,&ld)==3 && f==1.25f && d==-3.5 && ld==1.03125L);
    int values[8]={0};
    CHECK(sscanf("1 2 3 4 5 6 7 8","%d%d%d%d%d%d%d%d",values,values+1,values+2,values+3,values+4,values+5,values+6,values+7)==8);
    for(int i=0;i<8;i++)CHECK(values[i]==i+1);
    char raw[4]={'!','!','!','!'};
    CHECK(sscanf("ignore xyz","%*s %3c",raw)==1 && raw[0]=='x' && raw[2]=='z' && raw[3]=='!');
    CHECK(sscanf("%%17","%%%%%d",&number)==1 && number==17);
    number=99;CHECK(sscanf("","%d",&number)==-1 && number==99);
    CHECK(sscanf("no","%d",&number)==0 && number==99);
    int second=88;CHECK(sscanf("7 no","%d %d",&number,&second)==1 && number==7 && second==88);
    number=99;unsigned wide[4]={0};
    CHECK(sscanf("7 x","%d %ls",&number,wide)==-1 && *__errno_location()==22 && number==99);
    CHECK(sscanf("1","%1$d",&number)==-1 && *__errno_location()==22);
    int fd=(int)syscall(319,"stdio-probe",0);CHECK(fd>=0);
    void *file=fdopen(fd,"w+");CHECK(file);
    CHECK(__fprintf_chk(file,1,"%ld %.2f %s",4294967296L,1.25,"ok")==18);
    CHECK(lseek(fd,0,0)==0);
    char result[32]={0};CHECK(fread(result,1,18,file)==18 && !strcmp(result,"4294967296 1.25 ok"));
    CHECK(lseek(fd,0,0)==0);
    static char large_text[8193];static char readback[8193];
    for(unsigned i=0;i<8192;i++)large_text[i]='a'+i%26;
    CHECK(__fprintf_chk(file,0,"%s",large_text)==8192);
    CHECK(lseek(fd,0,0)==0 && fread(readback,1,8192,file)==8192 && !strcmp(large_text,readback));
    CHECK(close(fd)==0);
    CHECK(__fprintf_chk(file,0,"error")==-1 && *__errno_location()==9 && ferror(file));
    CHECK(fclose(file)==0);
    return 0;
}
