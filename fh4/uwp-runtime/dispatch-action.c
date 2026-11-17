/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Normal-priority CoreDispatcher actions. No game/account emulation. */
#include "private.h"
#include <asyncinfo.h>

struct dispatch_action {
    IAsyncAction IAsyncAction_iface;
    IAsyncInfo IAsyncInfo_iface;
    LONG refs;
    SRWLOCK lock;
    UINT32 id;
    AsyncStatus status;
    HRESULT error;
    BOOL closed, assigned, notified;
    IDispatchedHandler *callback;
    IAsyncActionCompletedHandler *completed;
    struct dispatch_action *next;
};
static LONG next_action_id;
static struct dispatch_action *from_action(IAsyncAction *a)
{ return CONTAINING_RECORD(a,struct dispatch_action,IAsyncAction_iface); }
static struct dispatch_action *from_info(IAsyncInfo *a)
{ return CONTAINING_RECORD(a,struct dispatch_action,IAsyncInfo_iface); }
static ULONG WINAPI action_add(IAsyncAction *a) { return InterlockedIncrement(&from_action(a)->refs); }
static ULONG WINAPI action_release(IAsyncAction *a)
{
    struct dispatch_action *o=from_action(a); ULONG refs=InterlockedDecrement(&o->refs);
    if(!refs) { if(o->callback)o->callback->lpVtbl->Release(o->callback);
        if(o->completed)o->completed->lpVtbl->Release(o->completed); free(o); }
    return refs;
}
static HRESULT WINAPI action_qi(IAsyncAction *a,REFIID id,void **out)
{
    if(!out)return E_POINTER;
    *out=NULL;
    if(IsEqualGUID(id,&IID_IUnknown)||IsEqualGUID(id,&IID_IInspectable)||
       IsEqualGUID(id,&IID_IAgileObject)||IsEqualGUID(id,&IID_IAsyncAction))*out=a;
    else if(IsEqualGUID(id,&IID_IAsyncInfo))*out=&from_action(a)->IAsyncInfo_iface;
    else return E_NOINTERFACE;
    action_add(a); return S_OK;
}
static HRESULT WINAPI action_iids(IAsyncAction *a,ULONG *n,IID **ids)
{
    if(!n||!ids)return E_POINTER;
    *n=0;*ids=CoTaskMemAlloc(2*sizeof(IID));if(!*ids)return E_OUTOFMEMORY;
    (*ids)[0]=IID_IAsyncAction;(*ids)[1]=IID_IAsyncInfo;*n=2;return S_OK;
}
static HRESULT WINAPI action_name(IAsyncAction *a,HSTRING *s)
{ if(!s)return E_POINTER;*s=NULL;return E_NOTIMPL; }
static HRESULT WINAPI action_trust(IAsyncAction *a,TrustLevel *t)
{ if(!t)return E_POINTER;*t=BaseTrust;return S_OK; }
static void notify_action(struct dispatch_action *o)
{
    IAsyncActionCompletedHandler *handler=NULL; AsyncStatus status;
    AcquireSRWLockExclusive(&o->lock);
    status=o->status;
    if(status!=Started && !o->closed && o->completed && !o->notified) {
        o->notified=TRUE;handler=o->completed;handler->lpVtbl->AddRef(handler);
        action_add(&o->IAsyncAction_iface);
    }
    ReleaseSRWLockExclusive(&o->lock);
    if(handler) {
        handler->lpVtbl->Invoke(handler,&o->IAsyncAction_iface,status);
        handler->lpVtbl->Release(handler);action_release(&o->IAsyncAction_iface);
    }
}
static void finish_action(struct dispatch_action *o,HRESULT hr)
{
    AcquireSRWLockExclusive(&o->lock);
    if(o->status==Started) { o->status=FAILED(hr)?Error:Completed;o->error=hr; }
    ReleaseSRWLockExclusive(&o->lock);notify_action(o);
}
static HRESULT WINAPI action_put(IAsyncAction *a,IAsyncActionCompletedHandler *handler)
{
    struct dispatch_action *o=from_action(a);HRESULT hr=S_OK;
    if(!handler)return E_POINTER;
    AcquireSRWLockExclusive(&o->lock);
    if(o->closed)hr=RO_E_CLOSED;
    else if(o->assigned)hr=E_ILLEGAL_DELEGATE_ASSIGNMENT;
    else {o->assigned=TRUE;o->completed=handler;handler->lpVtbl->AddRef(handler);}
    ReleaseSRWLockExclusive(&o->lock);
    if(SUCCEEDED(hr))notify_action(o);return hr;
}
static HRESULT WINAPI action_get(IAsyncAction *a,IAsyncActionCompletedHandler **handler)
{
    struct dispatch_action *o=from_action(a);HRESULT hr=S_OK;
    if(!handler)return E_POINTER;*handler=NULL;
    AcquireSRWLockShared(&o->lock);
    if(o->closed)hr=RO_E_CLOSED;
    else if((*handler=o->completed))(*handler)->lpVtbl->AddRef(*handler);
    ReleaseSRWLockShared(&o->lock);return hr;
}
static HRESULT WINAPI action_results(IAsyncAction *a)
{
    struct dispatch_action *o=from_action(a);HRESULT hr;
    AcquireSRWLockShared(&o->lock);
    hr=o->closed?RO_E_CLOSED:o->status==Started?E_ILLEGAL_METHOD_CALL:o->error;
    ReleaseSRWLockShared(&o->lock);return hr;
}
static const IAsyncActionVtbl action_vtbl={action_qi,action_add,action_release,action_iids,
    action_name,action_trust,action_put,action_get,action_results};
static HRESULT WINAPI info_qi(IAsyncInfo *a,REFIID id,void **out)
{ return action_qi(&from_info(a)->IAsyncAction_iface,id,out); }
static ULONG WINAPI info_add(IAsyncInfo *a) {return action_add(&from_info(a)->IAsyncAction_iface);}
static ULONG WINAPI info_release(IAsyncInfo *a) {return action_release(&from_info(a)->IAsyncAction_iface);}
static HRESULT WINAPI info_iids(IAsyncInfo *a,ULONG *n,IID **ids)
{return action_iids(&from_info(a)->IAsyncAction_iface,n,ids);}
static HRESULT WINAPI info_name(IAsyncInfo *a,HSTRING *s) {return action_name(&from_info(a)->IAsyncAction_iface,s);}
static HRESULT WINAPI info_trust(IAsyncInfo *a,TrustLevel *t) {return action_trust(&from_info(a)->IAsyncAction_iface,t);}
#define INFO_GET(name,type,field) \
static HRESULT WINAPI info_##name(IAsyncInfo *a,type *v) { \
 struct dispatch_action *o=from_info(a);HRESULT hr=S_OK;if(!v)return E_POINTER; \
 AcquireSRWLockShared(&o->lock);if(o->closed)hr=RO_E_CLOSED;else *v=o->field; \
 ReleaseSRWLockShared(&o->lock);return hr; }
INFO_GET(id,UINT32,id)
INFO_GET(status,AsyncStatus,status)
INFO_GET(error,HRESULT,error)
static HRESULT WINAPI info_cancel(IAsyncInfo *a)
{
    struct dispatch_action *o=from_info(a);HRESULT hr=S_OK;
    AcquireSRWLockExclusive(&o->lock);
    if(o->closed)hr=RO_E_CLOSED;
    else if(o->status==Started){o->status=Canceled;o->error=HRESULT_FROM_WIN32(ERROR_CANCELLED);}
    ReleaseSRWLockExclusive(&o->lock);if(SUCCEEDED(hr))notify_action(o);return hr;
}
static HRESULT WINAPI info_close(IAsyncInfo *a)
{
    struct dispatch_action *o=from_info(a);HRESULT hr=S_OK;
    IAsyncActionCompletedHandler *completed=NULL;IDispatchedHandler *callback=NULL;
    AcquireSRWLockExclusive(&o->lock);
    if(o->status==Started)hr=E_ILLEGAL_METHOD_CALL;
    else {o->closed=TRUE;completed=o->completed;o->completed=NULL;callback=o->callback;o->callback=NULL;}
    ReleaseSRWLockExclusive(&o->lock);
    if(completed)completed->lpVtbl->Release(completed);
    if(callback)callback->lpVtbl->Release(callback);
    return hr;
}
static const IAsyncInfoVtbl info_vtbl={info_qi,info_add,info_release,info_iids,info_name,
    info_trust,info_id,info_status,info_error,info_cancel,info_close};

void dispatcher_dispatch_one(struct dispatcher_impl *d)
{
    struct dispatch_action *o;IDispatchedHandler *callback=NULL;HRESULT hr=S_OK;
    AcquireSRWLockExclusive(&d->queue_lock);
    o=d->queue_head;
    if(o){d->queue_head=o->next;if(!d->queue_head)d->queue_tail=NULL;}
    ReleaseSRWLockExclusive(&d->queue_lock);
    if(!o)return;
    AcquireSRWLockShared(&o->lock);
    if(!o->closed && o->status==Started){callback=o->callback;callback->lpVtbl->AddRef(callback);}
    ReleaseSRWLockShared(&o->lock);
    if(callback){hr=callback->lpVtbl->Invoke(callback);callback->lpVtbl->Release(callback);}
    finish_action(o,hr);action_release(&o->IAsyncAction_iface);
}
void dispatcher_stop(struct dispatcher_impl *d)
{
    struct dispatch_action *list,*next;
    if(!d)return;
    AcquireSRWLockExclusive(&d->queue_lock);
    d->stopped=TRUE;d->window=NULL;list=d->queue_head;d->queue_head=d->queue_tail=NULL;
    ReleaseSRWLockExclusive(&d->queue_lock);
    /* RunAsync completes without executing queued work when shutting down. */
    for(;list;list=next){next=list->next;finish_action(list,S_OK);action_release(&list->IAsyncAction_iface);}
}
HRESULT dispatcher_queue_action(struct dispatcher_impl *d,CoreDispatcherPriority priority,
    IDispatchedHandler *callback,IAsyncAction **out)
{
    struct dispatch_action *o;HRESULT hr=S_OK;BOOL stopped;
    if(!out)return E_POINTER;*out=NULL;if(!callback)return E_POINTER;
    if(priority<CoreDispatcherPriority_Idle||priority>CoreDispatcherPriority_High)return E_INVALIDARG;
    /* Priority/input arbitration and idle callbacks remain explicit gaps. */
    if(priority!=CoreDispatcherPriority_Normal)return E_NOTIMPL;
    o=calloc(1,sizeof(*o));if(!o)return E_OUTOFMEMORY;
    o->IAsyncAction_iface.lpVtbl=&action_vtbl;o->IAsyncInfo_iface.lpVtbl=&info_vtbl;
    o->refs=1;o->id=(UINT32)InterlockedIncrement(&next_action_id);o->status=Started;
    o->callback=callback;callback->lpVtbl->AddRef(callback);
    AcquireSRWLockExclusive(&d->queue_lock);stopped=d->stopped;
    if(!stopped) {
        if(!PostMessageW(d->window,FH4_DISPATCH_MESSAGE,0,0))hr=HRESULT_FROM_WIN32(GetLastError());
        else {action_add(&o->IAsyncAction_iface);if(d->queue_tail)d->queue_tail->next=o;
            else d->queue_head=o;d->queue_tail=o;}
    }
    ReleaseSRWLockExclusive(&d->queue_lock);
    if(FAILED(hr)){action_release(&o->IAsyncAction_iface);return hr;}
    if(stopped)finish_action(o,S_OK);*out=&o->IAsyncAction_iface;return S_OK;
}
