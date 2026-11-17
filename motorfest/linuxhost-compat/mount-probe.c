/* SPDX-License-Identifier: AGPL-3.0-only */
typedef unsigned long size_t;
struct mntent { char *fsname,*dir,*type,*opts;int freq,passno; };
extern void *setmntent(const char*,const char*),*fdopen(int,const char*);
extern int endmntent(void*),feof(void*),ferror(void*),close(int),strcmp(const char*,const char*);
extern struct mntent *getmntent(void*);
extern char *getenv(const char*);
extern long syscall(long,...),write(int,const void*,size_t),lseek(int,long,int);
extern size_t strlen(const char*);
extern int *__errno_location(void);
#define CHECK(x) do{if(!(x))return __LINE__;}while(0)
long compat_test(void)
{
    const char *path=getenv("LINUXHOST_MOUNT_FIXTURE");CHECK(path);
    void *file=setmntent(path,"r");CHECK(file);
    struct mntent *entry=getmntent(file);CHECK(entry);
    CHECK(!strcmp(entry->fsname,"/dev/test name") && !strcmp(entry->dir,"/test\tmount\nname"));
    CHECK(!strcmp(entry->type,"fixture") && !strcmp(entry->opts,"rw,tag=one\\two\\three"));
    CHECK(entry->freq==1 && entry->passno==2);
    entry=getmntent(file);CHECK(entry && !strcmp(entry->fsname,"minimal"));
    CHECK(!*entry->dir && !*entry->type && !*entry->opts && !entry->freq && !entry->passno);
    entry=getmntent(file);CHECK(entry && !strcmp(entry->type,"autofs") && !strcmp(entry->opts,"ignore-me"));
    entry=getmntent(file);CHECK(entry && !strcmp(entry->fsname,"final\\0"));
    CHECK(entry->freq==3 && !entry->passno);
    CHECK(!getmntent(file) && feof(file) && !ferror(file));
    CHECK(endmntent(file)==1 && endmntent((void*)0)==1);
    CHECK(!setmntent("/missing-motorfest-probe-dir-13b67e/mounts","r") && *__errno_location()==2);
    CHECK(!setmntent(path,"invalid") && *__errno_location()==22);

    /* A long real record must not be silently truncated or split into entries. */
    int fd=(int)syscall(319,"mount-probe",0);CHECK(fd>=0);
    static char record[12000];
    for(unsigned i=0;i<10000;i++)record[i]='x';
    const char *tail=" /long fixture rw 4 5\nlast /end fixture ro";
    for(unsigned i=0;tail[i];i++)record[10000+i]=tail[i];
    size_t size=strlen(record);CHECK(write(fd,record,size)==(long)size && lseek(fd,0,0)==0);
    file=fdopen(fd,"r");CHECK(file);
    entry=getmntent(file);CHECK(entry && strlen(entry->fsname)==10000 && !strcmp(entry->dir,"/long"));
    CHECK(entry->freq==4 && entry->passno==5);
    entry=getmntent(file);CHECK(entry && !strcmp(entry->fsname,"last") && !strcmp(entry->dir,"/end"));
    CHECK(!getmntent(file) && feof(file));CHECK(endmntent(file)==1);
    fd=(int)syscall(319,"mount-error",0);CHECK(fd>=0);file=fdopen(fd,"r");CHECK(file);
    CHECK(close(fd)==0);
    CHECK(!getmntent(file) && ferror(file) && !feof(file) && *__errno_location()==9);
    CHECK(endmntent(file)==1);
    return 0;
}
