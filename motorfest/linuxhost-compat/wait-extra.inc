/* SPDX-License-Identifier: AGPL-3.0-only
 * Serialized upstream fork/exec model: reap any completed child, never wait on
 * the first table slot while a later child has already exited. Not POSIX fork.
 */
static int reap_child(struct child *c,int *status)
{
    int pid=c->pid;
    if(c->process){CloseHandle(c->process);c->process=NULL;}
    if(c->thread){CloseHandle(c->thread);c->thread=NULL;}
    c->state=CHILD_EXITED;c->reaped=1;
    if(status)*status=c->status;
    return pid;
}
static SYSV int lx_waitpid(int pid,int *status,int options)
{
    if(options & ~1){*linuxhost_errno()=95;return -1;}
    if(pid==0 || pid < -1){*linuxhost_errno()=95;return -1;}
    for(;;)
    {
        HANDLE handles[ARRAY_SIZE(children)];DWORD count=0;
        int found=0;
        for(unsigned i=0;i<child_count;i++)
        {
            struct child *c=&children[i];DWORD code;
            if((pid!=-1 && c->pid!=pid)||c->reaped)continue;
            found=1;
            if(c->traced && !c->awaiting_cont && c->stop_index<c->stop_count)
            {
                int stop=c->stops[c->stop_index++],st;
                if(stop==STOP_SIGSTOP)st=(19<<8)|0x7f;
                else if(c->options&0x10)st=((5|(4<<8))<<8)|0x7f;
                else st=(5<<8)|0x7f;
                c->awaiting_cont=1;if(status)*status=st;return c->pid;
            }
            if(c->state==CHILD_EXITED)return reap_child(c,status);
            if(c->state==CHILD_EXECED && c->process)
            {
                DWORD waited=WaitForSingleObject(c->process,0);
                if(waited==WAIT_OBJECT_0)
                {
                    if(!GetExitCodeProcess(c->process,&code)){*linuxhost_errno()=5;return -1;}
                    c->status=c->terminated_signal ? c->terminated_signal:((code&0xff)<<8);
                    return reap_child(c,status);
                }
                if(waited!=WAIT_TIMEOUT){*linuxhost_errno()=5;return -1;}
                if(!c->awaiting_cont && !c->suspended)handles[count++]=c->process;
            }
        }
        if(!found){*linuxhost_errno()=10;return -1;}
        if(options&1)return 0;
        if(!count){*linuxhost_errno()=95;return -1;} /* No child can progress. */
        DWORD waited=WaitForMultipleObjects(count,handles,FALSE,INFINITE);
        if(waited<WAIT_OBJECT_0 || waited>=WAIT_OBJECT_0+count){*linuxhost_errno()=5;return -1;}
    }
}
