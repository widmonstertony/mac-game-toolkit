/* SPDX-License-Identifier: AGPL-3.0-only
 * Native macOS child supervisor. Never installed into an engine globally.
 * Report actual POSIX wait status, translated to Linux signal numbers; Wine's
 * spawnvp result alone cannot distinguish a signal from ordinary exit 255.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
extern char **environ;
static int linux_signal(int sig)
{
    static const int map[32]={0,1,2,3,4,5,6,0,8,9,7,11,31,13,14,15,
        23,19,20,18,17,21,22,29,24,25,26,27,28,0,10,12};
    return sig>0 && sig<32 ? map[sig]:0;
}
static int linux_error(int error)
{
    /* Supervisor operations below: preserve only identical/common numbers. */
    if(error>=0 && error<=34)return error;
    if(error==ENOSYS)return 38;
    if(error==ELOOP)return 40;
    if(error==ENAMETOOLONG)return 36;
    return 5; /* EIO, do not pass unrelated Darwin errno as Linux errno. */
}
int main(int argc,char **argv)
{
    if(argc!=4)return 64;
    int fd=open(argv[1],O_WRONLY|O_NOFOLLOW|O_CLOEXEC);
    struct stat st;
    if(fd<0)return 65;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||st.st_nlink!=1||st.st_size!=16)
    {close(fd);return 66;}
    uint32_t reply[4]={0x4c485350,1,0,0};
    pid_t child;int status=0,error=0;
    if(chdir(argv[2]))error=errno;
    posix_spawnattr_t attr;
    int initialized=0;
    if(!error){error=posix_spawnattr_init(&attr);initialized=!error;}
    if(!error)
    {
        sigset_t defaults,mask;
        sigemptyset(&defaults);sigaddset(&defaults,SIGINT);sigaddset(&defaults,SIGQUIT);
        sigemptyset(&mask);
        error=posix_spawnattr_setsigdefault(&attr,&defaults);
        if(!error)error=posix_spawnattr_setsigmask(&attr,&mask);
        if(!error)error=posix_spawnattr_setflags(&attr,POSIX_SPAWN_SETSIGDEF|POSIX_SPAWN_SETSIGMASK);
        char *args[]={"/bin/sh","-c",argv[3],NULL};
        if(!error)error=posix_spawn(&child,"/bin/sh",NULL,&attr,args,environ);
        if(!error)
        {
            pid_t result;
            do{result=waitpid(child,&status,0);}while(result<0 && errno==EINTR);
            if(result<0)error=errno;
            else if(WIFEXITED(status))reply[2]=(uint32_t)WEXITSTATUS(status)<<8;
            else if(WIFSIGNALED(status))
            {
                int sig=linux_signal(WTERMSIG(status));
                if(!sig)error=ENOTSUP;
                else reply[2]=sig|(WCOREDUMP(status)?128:0);
            }
            else error=EIO;
        }
    }
    if(initialized)posix_spawnattr_destroy(&attr);
    reply[3]=linux_error(error);
    /* A truncated/failed response is rejected by the PE side, never success. */
    ssize_t result;do{result=pwrite(fd,reply,sizeof(reply),0);}while(result<0 && errno==EINTR);
    close(fd);return result==sizeof(reply) ? 0:67;
}
