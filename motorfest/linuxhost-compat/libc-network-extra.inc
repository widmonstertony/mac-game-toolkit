/* SPDX-License-Identifier: AGPL-3.0-only
 * Included in pinned libc_net.c to reuse its descriptor, address and errno
 * translation. No simulated network responses or authentication changes. */
static SYSV lx_long lx_sendto(int fd,const void *buf,lx_size_t len,int flags,
                            const void *address,lx_socklen_t address_len)
{
    HANDLE handle;
    int kind=linuxhost_fd_kind(fd,&handle),ret;
    struct sockaddr_storage ss;
    const struct sockaddr *to=NULL;
    if(!kind)return fail(9); /* EBADF */
    if(kind!=LINUXHOST_FD_SOCKET)return fail(88);
    if(len>0x7fffffff)return fail(90); /* never truncate an atomic datagram */
    /* MSG_NOSIGNAL is naturally satisfied by Winsock. Other Linux-only flags,
     * including per-call MSG_DONTWAIT, must not silently become blocking calls. */
    if(flags & ~(1|4|0x4000))return fail(95);
    if(address)
    {
        if(!addr_from_linux(address,address_len,&ss))return fail(22);
        if(ss.ss_family!=AF_INET && ss.ss_family!=AF_INET6)return fail(97);
        if(address_len<(ss.ss_family==AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6)))return fail(22);
        to=(const struct sockaddr*)&ss;
    }
    else address_len=0; /* connected socket */
    ret=sendto((SOCKET)(UINT_PTR)handle,buf,(int)len,flags & (MSG_OOB|MSG_DONTROUTE),to,(int)address_len);
    return ret==SOCKET_ERROR ? wsa_fail() : ret;
}

static SYSV int lx_gethostname(char *name,lx_size_t len)
{
    char actual[256];
    init_winsock();
    if(gethostname(actual,sizeof(actual)))return wsa_fail();
    actual[sizeof(actual)-1]=0;
    size_t needed=strlen(actual)+1;
    if(!name && len)return fail(14);
    if(len<needed)
    {
        if(len)memcpy(name,actual,(size_t)len);
        return fail(36); /* ENAMETOOLONG, including a zero-sized buffer */
    }
    memcpy(name,actual,needed);
    return 0;
}
