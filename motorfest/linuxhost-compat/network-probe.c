/* SPDX-License-Identifier: AGPL-3.0-only
 * Linux ELF: actual IPv4/IPv6 loopback datagrams, no external endpoint. */
typedef unsigned long size_t;
extern int socket(int,int,int),bind(int,const void*,unsigned),connect(int,const void*,unsigned);
extern int getsockname(int,void*,unsigned*),close(int),gethostname(char*,size_t);
extern long sendto(int,const void*,size_t,int,const void*,unsigned);
extern long recvfrom(int,void*,size_t,int,void*,unsigned*);
extern int *__errno_location(void);
extern int *__h_errno_location(void);
struct hostent {char *name,**aliases;int type,length;char **addresses;};
extern struct hostent *gethostbyname(const char *);
extern int pthread_create(unsigned long*,const void*,void *(*)(void*),void*);
extern int pthread_join(unsigned long,void**),sched_yield(void);
struct pollfd {int fd;short events,revents;};
extern int poll(struct pollfd*,unsigned long,int);
struct in4 {unsigned short family,port;unsigned char address[4],padding[8];};
struct in6 {unsigned short family,port;unsigned flow;unsigned char address[16];unsigned scope;};
#define CHECK(x) do {if(!(x))return __LINE__;}while(0)

static long datagrams(int family)
{
    int rx=socket(family,2,17),tx=socket(family,2,17);
    CHECK(rx>=0 && tx>=0);
    struct in4 a4={2,0,{127,0,0,1},{0}};
    struct in6 a6={10,0,0,{0},0};a6.address[15]=1;
    void *address=family==2 ? (void*)&a4 : (void*)&a6;
    unsigned size=family==2 ? sizeof(a4) : sizeof(a6),got=size;
    CHECK(bind(rx,address,size)==0);
    CHECK(getsockname(rx,address,&got)==0 && got==size);
    CHECK(sendto(tx,"probe",5,0x4000,address,size)==5);
    struct pollfd wait={rx,1,0};CHECK(poll(&wait,1,2000)==1);
    char buffer[16]={0};unsigned char source[128]={0};unsigned source_len=128;
    CHECK(recvfrom(rx,buffer,sizeof(buffer),0,source,&source_len)==5);
    CHECK(buffer[0]=='p' && buffer[4]=='e' && *(unsigned short*)source==family && source_len==size);
    CHECK(sendto(tx,"",0,0,address,size)==0);
    CHECK(poll(&wait,1,2000)==1 && recvfrom(rx,buffer,sizeof(buffer),0,(void*)0,(void*)0)==0);
    CHECK(connect(tx,address,size)==0);
    CHECK(sendto(tx,"c",1,0,(void*)0,0)==1);
    CHECK(poll(&wait,1,2000)==1 && recvfrom(rx,buffer,sizeof(buffer),0,(void*)0,(void*)0)==1 && buffer[0]=='c');
    CHECK(sendto(tx,(void*)1,0x80000000UL,0,address,size)==-1 && *__errno_location()==90);
    CHECK(sendto(tx,"x",1,0x40,address,size)==-1 && *__errno_location()==95);
    CHECK(sendto(tx,"x",1,0,address,1)==-1 && *__errno_location()==22);
    CHECK(close(tx)==0 && close(rx)==0);
    CHECK(sendto(tx,"x",1,0,address,size)==-1 && *__errno_location()==9);
    return 0;
}
static void *resolve_thread(void *arg)
{
    unsigned char last=(unsigned long)arg;
    struct hostent *h=gethostbyname(last==1 ? "127.0.0.1" : "127.0.0.2");
    if(!h || h->type!=2 || h->length!=4 || !h->addresses || !h->addresses[0])return (void*)1;
    *__h_errno_location()=last;
    for(unsigned i=0;i<32;i++)
    {
        sched_yield();
        unsigned char *ip=(unsigned char*)h->addresses[0];
        if(ip[0]!=127 || ip[1] || ip[2] || ip[3]!=last || *__h_errno_location()!=last)return (void*)2;
    }
    return (void*)0;
}
long compat_test(void)
{
    char hostname[256],tiny[2]={'!','?'};
    CHECK(gethostname(hostname,sizeof(hostname))==0 && hostname[0]);
    CHECK(gethostname(tiny,1)==-1 && *__errno_location()==36 && tiny[1]=='?');
    CHECK(gethostname((void*)0,0)==-1 && *__errno_location()==36);
    struct hostent *local=gethostbyname("localhost");
    CHECK(local && local->name && local->aliases && local->type==2 && local->length==4);
    /* Legacy IPv4 resolver: providers may report HOST_NOT_FOUND or NO_DATA
     * for an IPv6 literal; neither may be turned into a fabricated IPv4 result. */
    CHECK(gethostbyname("::1")==0 && (*__h_errno_location()==1 || *__h_errno_location()==4));
    unsigned long threads[2];void *result;
    *__h_errno_location()=91;
    CHECK(!pthread_create(&threads[0],(void*)0,resolve_thread,(void*)1));
    CHECK(!pthread_create(&threads[1],(void*)0,resolve_thread,(void*)2));
    CHECK(!pthread_join(threads[0],&result) && !result);
    CHECK(!pthread_join(threads[1],&result) && !result && *__h_errno_location()==91);
    long r=datagrams(2);if(r)return r;
    r=datagrams(10);if(r)return r+1000;
    return 0;
}
