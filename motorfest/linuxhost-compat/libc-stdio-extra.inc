/* SPDX-License-Identifier: AGPL-3.0-only
 * Uses the exact private lx_file representation in pinned libc_io.c. */
int linuxhost_stream_write(void *stream,const void *bytes,size_t size)
{
    struct lx_file *file=stream;
    size_t total=0;
    if(!file)return fail(22);
    while(total<size)
    {
        long long n;
        if(file->fd==1 || file->fd==2)
        {
            DWORD written=0;
            if(!WriteFile(GetStdHandle(file->fd==1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE),
                          (const char*)bytes+total,(DWORD)(size-total),&written,NULL))n=-5;
            else n=written;
        }
        else n=linuxhost_write(file->fd,(const char*)bytes+total,size-total);
        if(n<=0){file->error=1;return fail(n<0 ? (int)-n : 5);}
        total+=(size_t)n;
    }
    return 0;
}
