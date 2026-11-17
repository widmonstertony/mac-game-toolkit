/* SPDX-License-Identifier: AGPL-3.0-only
 * Generic mtab reader on the pinned host's actual file descriptors.
 * No mount inventory is synthesized. getmntent storage is reused, as in libc.
 */
struct lx_mntent { char *fsname,*dir,*type,*opts; int freq,passno; };
static struct lx_mntent mount_entry;
static char *mount_line;
static size_t mount_capacity;

static SYSV struct lx_file *lx_setmntent(const char *path,const char *mode)
{
    if(!path || !mode || !*mode || !strchr("rwa",mode[0]))
    {fail(22);return NULL;}
    int flags=0x80000; /* O_CLOEXEC; real host handles are non-inheritable. */
    flags|=strchr(mode,'+') ? 2 : mode[0]=='r' ? 0 : 1;
    if(mode[0]=='w')flags|=0x240;
    if(mode[0]=='a')flags|=0x440;
    int fd=lx_open(path,flags,0644);
    if(fd<0)return NULL;
    struct lx_file *file=calloc(1,sizeof(*file));
    if(!file){linuxhost_close(fd);fail(12);return NULL;}
    file->fd=fd;file->ungot=-1;
    return file;
}

static SYSV int lx_endmntent(struct lx_file *file)
{
    if(file)lx_fclose(file);
    /* glibc's implementation deliberately returns 1, including for NULL. */
    return 1;
}

static int mount_read_line(struct lx_file *file)
{
    size_t length=0;
    for(;;)
    {
        if(length+1>=mount_capacity)
        {
            if(mount_capacity>SIZE_MAX/2){fail(75);return -1;}
            size_t capacity=mount_capacity ? mount_capacity*2 : 256;
            char *grown=realloc(mount_line,capacity);
            if(!grown){fail(12);return -1;}
            mount_line=grown;mount_capacity=capacity;
        }
        unsigned char c;
        long long count;
        if(file->ungot>=0){c=file->ungot;file->ungot=-1;count=1;}
        else count=linuxhost_read(file->fd,&c,1);
        if(count<0){file->error=1;fail((int)-count);return -1;}
        if(!count){file->eof=1;if(!length)return 0;break;}
        if(c=='\n')break;
        mount_line[length++]=c;
    }
    mount_line[length]=0;
    return 1;
}

static void mount_decode(char *text)
{
    char *read=text,*write=text;
    while(*read)
    {
        if(*read=='\\' && read[1]=='\\'){*write++='\\';read+=2;continue;}
        if(*read=='\\' && read[1] && read[2] && read[3])
        {
            const char *codes[]={"040","011","012","134"};
            const char values[]={' ','\t','\n','\\'};
            unsigned i;
            for(i=0;i<4;i++)if(!memcmp(read+1,codes[i],3))break;
            if(i<4){*write++=values[i];read+=4;continue;}
        }
        *write++=*read++;
    }
    *write=0;
}

static int mount_option(const char *list,const char *wanted)
{
    size_t n=strlen(wanted);
    for(const char *p=list;*p;)
    {
        const char *end=strchr(p,',');
        size_t length=end ? (size_t)(end-p) : strlen(p);
        if(length>=n && !memcmp(p,wanted,n) && (length==n || p[n]=='='))return 1;
        if(!end)break;
        p=end+1;
    }
    return 0;
}

static SYSV struct lx_mntent *lx_getmntent(struct lx_file *file)
{
    if(!file){fail(22);return NULL;}
    for(;;)
    {
        if(mount_read_line(file)<=0)return NULL;
        char *p=mount_line;
        while(*p==' ' || *p=='\t')p++;
        if(!*p || *p=='#')continue;
        char *fields[4];
        for(unsigned i=0;i<4;i++)
        {
            fields[i]=p;
            while(*p && *p!=' ' && *p!='\t')p++;
            if(*p)*p++=0;
            while(*p==' ' || *p=='\t')p++;
            mount_decode(fields[i]);
        }
        struct lx_mntent value={fields[0],fields[1],fields[2],fields[3],0,0};
        /* int is 32-bit in both ABIs; no Linux long/varargs adaptation needed. */
        sscanf(p,"%d %d",&value.freq,&value.passno);
        if(!strcmp(value.type,"autofs") && mount_option(value.opts,"ignore"))continue;
        mount_entry=value;
        return &mount_entry;
    }
}
