/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Included by the lifecycle probe; exercises the public COM interfaces only. */
#include <asyncinfo.h>
static LONG task_refs=1, done_refs=1;
static unsigned task_calls, done_calls;
static DWORD dispatch_owner;
static HRESULT task_result;
static AsyncStatus done_status;
static IAsyncAction *worker_action, *shutdown_action;
static HRESULT WINAPI task_qi(IDispatchedHandler *p,REFIID id,void **out)
{ *out=NULL; return E_NOINTERFACE; }
static ULONG WINAPI task_add(IDispatchedHandler *p) {return InterlockedIncrement(&task_refs);}
static ULONG WINAPI task_release(IDispatchedHandler *p) {return InterlockedDecrement(&task_refs);}
static HRESULT WINAPI task_invoke(IDispatchedHandler *p)
{ CHECK(GetCurrentThreadId()==dispatch_owner);++task_calls;return task_result; }
static const IDispatchedHandlerVtbl task_vtbl={task_qi,task_add,task_release,task_invoke};
static IDispatchedHandler task={&task_vtbl};
static HRESULT WINAPI done_qi(IAsyncActionCompletedHandler *p,REFIID id,void **out)
{*out=NULL;return E_NOINTERFACE;}
static ULONG WINAPI done_add(IAsyncActionCompletedHandler *p) {return InterlockedIncrement(&done_refs);}
static ULONG WINAPI done_release(IAsyncActionCompletedHandler *p) {return InterlockedDecrement(&done_refs);}
static HRESULT WINAPI done_invoke(IAsyncActionCompletedHandler *p,IAsyncAction *a,AsyncStatus status)
{
    IAsyncInfo *info=NULL;AsyncStatus actual;
    OK(a->lpVtbl->QueryInterface(a,&IID_IAsyncInfo,(void**)&info));
    OK(info->lpVtbl->get_Status(info,&actual));CHECK(actual==status);
    info->lpVtbl->Release(info);++done_calls;done_status=status;return S_OK;
}
static const IAsyncActionCompletedHandlerVtbl done_vtbl={done_qi,done_add,done_release,done_invoke};
static IAsyncActionCompletedHandler done={&done_vtbl};
static DWORD WINAPI enqueue_worker(void *arg)
{
    CHECK(GetCurrentThreadId()!=dispatch_owner);
    OK(ICoreDispatcher_RunAsync((ICoreDispatcher*)arg,CoreDispatcherPriority_Normal,&task,&worker_action));
    return 0;
}
static void dispatch_tests(ICoreDispatcher *d)
{
    IAsyncAction *a=NULL;IAsyncInfo *info=NULL;AsyncStatus status;HRESULT error;HANDLE thread;
    dispatch_owner=GetCurrentThreadId();task_calls=done_calls=0;task_result=S_OK;
    thread=CreateThread(NULL,0,enqueue_worker,d,0,NULL);CHECK(thread);
    CHECK(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);CloseHandle(thread);
    a=worker_action;CHECK(a && !task_calls);
    OK(a->lpVtbl->QueryInterface(a,&IID_IAsyncInfo,(void**)&info));
    OK(info->lpVtbl->get_Status(info,&status));CHECK(status==Started);
    CHECK(a->lpVtbl->GetResults(a)==E_ILLEGAL_METHOD_CALL);
    CHECK(info->lpVtbl->Close(info)==E_ILLEGAL_METHOD_CALL);
    OK(a->lpVtbl->put_Completed(a,&done));
    OK(ICoreDispatcher_ProcessEvents(d,CoreProcessEventsOption_ProcessAllIfPresent));
    CHECK(task_calls==1 && done_calls==1 && done_status==Completed);
    OK(a->lpVtbl->GetResults(a));
    CHECK(a->lpVtbl->put_Completed(a,&done)==E_ILLEGAL_DELEGATE_ASSIGNMENT);
    OK(info->lpVtbl->Close(info));
    CHECK(info->lpVtbl->get_Status(info,&status)==RO_E_CLOSED);
    CHECK(a->lpVtbl->GetResults(a)==RO_E_CLOSED);
    info->lpVtbl->Release(info);CHECK(a->lpVtbl->Release(a)==0);
    CHECK(task_refs==1 && done_refs==1);

    /* A failed callback is an Error action. Late subscription fires once. */
    task_result=E_ABORT;
    OK(ICoreDispatcher_RunAsync(d,CoreDispatcherPriority_Normal,&task,&a));
    OK(ICoreDispatcher_ProcessEvents(d,CoreProcessEventsOption_ProcessAllIfPresent));
    CHECK(a->lpVtbl->GetResults(a)==E_ABORT);
    OK(a->lpVtbl->put_Completed(a,&done));CHECK(done_calls==2 && done_status==Error);
    OK(a->lpVtbl->QueryInterface(a,&IID_IAsyncInfo,(void**)&info));
    OK(info->lpVtbl->get_ErrorCode(info,&error));CHECK(error==E_ABORT);
    info->lpVtbl->Release(info);CHECK(a->lpVtbl->Release(a)==0);

    /* Cancellation before dispatch must not invoke the callback. */
    OK(ICoreDispatcher_RunAsync(d,CoreDispatcherPriority_Normal,&task,&a));
    OK(a->lpVtbl->put_Completed(a,&done));
    OK(a->lpVtbl->QueryInterface(a,&IID_IAsyncInfo,(void**)&info));
    OK(info->lpVtbl->Cancel(info));CHECK(done_calls==3 && done_status==Canceled);
    OK(ICoreDispatcher_ProcessEvents(d,CoreProcessEventsOption_ProcessAllIfPresent));
    CHECK(task_calls==2 && done_calls==3);
    CHECK(a->lpVtbl->GetResults(a)==HRESULT_FROM_WIN32(ERROR_CANCELLED));
    info->lpVtbl->Release(info);CHECK(a->lpVtbl->Release(a)==0);
    CHECK(task_refs==1 && done_refs==1);
    a=(void*)1;
    CHECK(ICoreDispatcher_RunAsync(d,(CoreDispatcherPriority)99,&task,&a)==E_INVALIDARG && !a);
    CHECK(ICoreDispatcher_RunAsync(d,CoreDispatcherPriority_Low,&task,&a)==E_NOTIMPL && !a);
    puts("DISPATCH_ACTION_PASS worker_to_owner=true cancellation=true failure=true completion=true refs=true");
}
static void queue_shutdown_test(ICoreDispatcher *d)
{
    task_calls=0;task_result=S_OK;
    OK(ICoreDispatcher_RunAsync(d,CoreDispatcherPriority_Normal,&task,&shutdown_action));
}
static void verify_shutdown_test(ICoreDispatcher *d)
{
    if(shutdown_action) {
        CHECK(task_calls==0);OK(shutdown_action->lpVtbl->GetResults(shutdown_action));
        CHECK(shutdown_action->lpVtbl->Release(shutdown_action)==0);shutdown_action=NULL;
        IAsyncAction *a=NULL;
        OK(ICoreDispatcher_RunAsync(d,CoreDispatcherPriority_Normal,&task,&a));
        OK(a->lpVtbl->GetResults(a));CHECK(task_calls==0);CHECK(a->lpVtbl->Release(a)==0);
        CHECK(task_refs==1 && done_refs==1);
        puts("DISPATCH_SHUTDOWN_PASS callbacks_not_invoked=true refs=true");
    }
}
