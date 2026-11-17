/* LGPL-2.1-or-later. Test build only; synthetic providers, no user input. */
struct cft_provider { IGameControllerProvider iface; LONG refs; UINT16 vendor, product; };
struct cft_factory
{
    ICustomGameControllerFactory iface; LONG refs;
    unsigned created, added, removed;
    BOOL decline, fail, reenter, remove_on_add;
    HANDLE entered, resume;
};
struct cft_inner { IInspectable iface; LONG refs; };
static unsigned cft_freed, cft_error;
static struct cft_provider cft_p[6];
static struct cft_factory cft_f[8];
static HRESULT WINAPI cft_i_qi(IInspectable *p, REFIID iid, void **out)
{
    *out = NULL;
    if (!IsEqualGUID(iid,&IID_IUnknown) && !IsEqualGUID(iid,&IID_IInspectable)) return E_NOINTERFACE;
    IInspectable_AddRef(p); *out=p; return S_OK;
}
static ULONG WINAPI cft_i_add(IInspectable *p) { return InterlockedIncrement(&((struct cft_inner*)p)->refs); }
static ULONG WINAPI cft_i_release(IInspectable *p)
{ LONG ref=InterlockedDecrement(&((struct cft_inner*)p)->refs); if(!ref) {cft_freed++;free(p);} return ref; }
static HRESULT WINAPI cft_i_ids(IInspectable *p,ULONG *n,IID **v) {*n=0;*v=NULL;return S_OK;}
static HRESULT WINAPI cft_i_name(IInspectable *p,HSTRING *s) {*s=NULL;return E_NOTIMPL;}
static HRESULT WINAPI cft_i_trust(IInspectable *p,TrustLevel *v) {*v=BaseTrust;return S_OK;}
static const IInspectableVtbl cft_inner_vtbl={cft_i_qi,cft_i_add,cft_i_release,cft_i_ids,cft_i_name,cft_i_trust};
static HRESULT WINAPI cft_p_qi(IGameControllerProvider *p,REFIID iid,void **out)
{ *out=NULL;if(!IsEqualGUID(iid,&IID_IUnknown) && !IsEqualGUID(iid,&IID_IGameControllerProvider))return E_NOINTERFACE;
  IGameControllerProvider_AddRef(p);*out=p;return S_OK; }
static ULONG WINAPI cft_p_add(IGameControllerProvider *p) {return InterlockedIncrement(&((struct cft_provider*)p)->refs);}
static ULONG WINAPI cft_p_release(IGameControllerProvider *p) {return InterlockedDecrement(&((struct cft_provider*)p)->refs);}
static HRESULT WINAPI cft_p_ids(IGameControllerProvider*p,ULONG*n,IID**v){*n=0;*v=NULL;return S_OK;}
static HRESULT WINAPI cft_p_name(IGameControllerProvider*p,HSTRING*s){*s=NULL;return E_NOTIMPL;}
static HRESULT WINAPI cft_p_trust(IGameControllerProvider*p,TrustLevel*v){*v=BaseTrust;return S_OK;}
static HRESULT WINAPI cft_p_version(IGameControllerProvider*p,GameControllerVersionInfo*v){memset(v,0,sizeof(*v));return S_OK;}
static HRESULT WINAPI cft_p_product(IGameControllerProvider*p,UINT16*v){*v=((struct cft_provider*)p)->product;return S_OK;}
static HRESULT WINAPI cft_p_vendor(IGameControllerProvider*p,UINT16*v){*v=((struct cft_provider*)p)->vendor;return S_OK;}
static HRESULT WINAPI cft_p_connected(IGameControllerProvider*p,boolean*v){*v=TRUE;return S_OK;}
static const IGameControllerProviderVtbl cft_provider_vtbl={cft_p_qi,cft_p_add,cft_p_release,cft_p_ids,cft_p_name,cft_p_trust,
 cft_p_version,cft_p_product,cft_p_vendor,cft_p_version,cft_p_connected};
static HRESULT WINAPI cft_f_qi(ICustomGameControllerFactory*p,REFIID iid,void**out)
{*out=NULL;if(!IsEqualGUID(iid,&IID_IUnknown)&&!IsEqualGUID(iid,&IID_ICustomGameControllerFactory))return E_NOINTERFACE;
 ICustomGameControllerFactory_AddRef(p);*out=p;return S_OK;}
static ULONG WINAPI cft_f_add(ICustomGameControllerFactory*p){return InterlockedIncrement(&((struct cft_factory*)p)->refs);}
static ULONG WINAPI cft_f_release(ICustomGameControllerFactory*p){return InterlockedDecrement(&((struct cft_factory*)p)->refs);}
static HRESULT WINAPI cft_f_ids(ICustomGameControllerFactory*p,ULONG*n,IID**v){*n=0;*v=NULL;return S_OK;}
static HRESULT WINAPI cft_f_name(ICustomGameControllerFactory*p,HSTRING*s){*s=NULL;return E_NOTIMPL;}
static HRESULT WINAPI cft_f_trust(ICustomGameControllerFactory*p,TrustLevel*v){*v=BaseTrust;return S_OK;}
static HRESULT WINAPI cft_f_create(ICustomGameControllerFactory*p,IGameControllerProvider*d,IInspectable**out)
{
    struct cft_factory *f=(void*)p; struct cft_inner *inner;
    f->created++; *out=NULL;
    if(f->entered) {SetEvent(f->entered);if(WaitForSingleObject(f->resume,5000)!=WAIT_OBJECT_0)cft_error=__LINE__;}
    if(f->fail)return E_FAIL;
    if(f->decline)return S_OK;
    if(!(inner=calloc(1,sizeof(*inner))))return E_OUTOFMEMORY;
    inner->iface.lpVtbl=&cft_inner_vtbl;inner->refs=1;*out=&inner->iface;return S_OK;
}
static DWORD WINAPI cft_register_worker(void *arg)
{return cf_register_hardware(&cft_f[7].iface,900,901);}
static HRESULT WINAPI cft_f_added(ICustomGameControllerFactory*p,IGameController*c)
{
    struct cft_factory *f=(void*)p; HANDLE thread; DWORD code; IGameController *mapped=NULL;
    f->added++;
    if(FAILED(IGameControllerFactoryManagerStatics2_TryGetFactoryControllerFromGameController(
        manager_factory,p,c,&mapped)) || mapped!=c)cft_error=__LINE__;
    if(mapped)IGameController_Release(mapped);
    if(f->reenter)
    {
        thread=CreateThread(NULL,0,cft_register_worker,NULL,0,NULL);
        if(!thread || WaitForSingleObject(thread,3000)!=WAIT_OBJECT_0)cft_error=__LINE__;
        else if(!GetExitCodeThread(thread,&code)||code!=S_OK)cft_error=__LINE__;
        if(thread)CloseHandle(thread);
        if(FAILED(cf_register_hardware(p,101,201)))cft_error=__LINE__;
    }
    if(f->remove_on_add)cf_provider_removed(&cft_p[3].iface);
    return S_OK;
}
static HRESULT WINAPI cft_f_removed(ICustomGameControllerFactory*p,IGameController*c)
{((struct cft_factory*)p)->removed++;return S_OK;}
static const ICustomGameControllerFactoryVtbl cft_factory_vtbl={cft_f_qi,cft_f_add,cft_f_release,cft_f_ids,cft_f_name,cft_f_trust,
 cft_f_create,cft_f_added,cft_f_removed};
static DWORD WINAPI cft_create_worker(void*arg){cf_provider_created(&cft_p[4].iface,NULL);return 0;}
#define CFT_CHECK(x) do {if(!(x))return __LINE__;} while(0)
DWORD WINAPI FH4CustomControllerSelfTest(void)
{
    unsigned i; LONG ref; HANDLE thread; GUID gip={0x1234,1,2,{3}};
    for(i=0;i<ARRAY_SIZE(cft_p);i++){cft_p[i].iface.lpVtbl=&cft_provider_vtbl;cft_p[i].refs=1;cft_p[i].vendor=100+i;cft_p[i].product=200+i;}
    for(i=0;i<ARRAY_SIZE(cft_f);i++){cft_f[i].iface.lpVtbl=&cft_factory_vtbl;cft_f[i].refs=1;}
    CFT_CHECK(cf_register_hardware(NULL,1,2)==E_INVALIDARG);
    CFT_CHECK(cf_register_hardware(&cft_f[0].iface,100,200)==S_OK);
    ref=cft_f[0].refs;
    CFT_CHECK(cf_register_hardware(&cft_f[0].iface,100,200)==S_OK && cft_f[0].refs==ref);
    cf_provider_created(&cft_p[1].iface,NULL); CFT_CHECK(!cft_f[0].created);
    manager_on_provider_created(&cft_p[0].iface); CFT_CHECK(cft_f[0].created==1 && cft_f[0].added==1);
    cf_provider_created(&cft_p[0].iface,NULL); CFT_CHECK(cft_f[0].created==1);
    /* Register after an existing device, then register recursively from callback. */
    cft_f[1].reenter=TRUE;
    CFT_CHECK(cf_register_hardware(&cft_f[1].iface,101,201)==S_OK);
    CFT_CHECK(cft_f[1].created==1 && cft_f[1].added==1 && !cft_error);
    CFT_CHECK(cf_register_gip(&cft_f[2].iface,&gip)==S_OK);
    cf_provider_created(&cft_p[2].iface,&gip); CFT_CHECK(cft_f[2].added==1);
    /* Two matching registrations for one factory produce only one controller. */
    CFT_CHECK(cf_register_hardware(&cft_f[2].iface,102,202)==S_OK && cft_f[2].created==1);
    cft_f[3].remove_on_add=TRUE;
    CFT_CHECK(cf_register_hardware(&cft_f[3].iface,103,203)==S_OK);
    cf_provider_created(&cft_p[3].iface,NULL);
    CFT_CHECK(cft_f[3].added==1 && cft_f[3].removed==1 && cft_p[3].refs==1);
    /* Remove concurrently while CreateGameController is executing outside lock. */
    cft_f[4].entered=CreateEventW(NULL,TRUE,FALSE,NULL);cft_f[4].resume=CreateEventW(NULL,TRUE,FALSE,NULL);
    CFT_CHECK(cft_f[4].entered && cft_f[4].resume);
    CFT_CHECK(cf_register_hardware(&cft_f[4].iface,104,204)==S_OK);
    thread=CreateThread(NULL,0,cft_create_worker,NULL,0,NULL);CFT_CHECK(thread);
    CFT_CHECK(WaitForSingleObject(cft_f[4].entered,3000)==WAIT_OBJECT_0);
    cf_provider_removed(&cft_p[4].iface); SetEvent(cft_f[4].resume);
    CFT_CHECK(WaitForSingleObject(thread,3000)==WAIT_OBJECT_0);CloseHandle(thread);
    CloseHandle(cft_f[4].entered);CloseHandle(cft_f[4].resume);
    CFT_CHECK(cft_f[4].added==1 && cft_f[4].removed==1 && cft_p[4].refs==1);
    cft_f[5].decline=TRUE;cft_f[6].fail=TRUE;
    CFT_CHECK(cf_register_hardware(&cft_f[5].iface,105,205)==S_OK);
    CFT_CHECK(cf_register_hardware(&cft_f[6].iface,105,205)==S_OK);
    cf_provider_created(&cft_p[5].iface,NULL);cf_provider_removed(&cft_p[5].iface);
    CFT_CHECK(cft_f[5].created==1 && !cft_f[5].added && !cft_f[5].removed);
    CFT_CHECK(cft_f[6].created==1 && !cft_f[6].added && !cft_f[6].removed);
    manager_on_provider_removed(&cft_p[0].iface);CFT_CHECK(cft_f[0].removed==1);
    for(i=1;i<3;i++){cf_provider_removed(&cft_p[i].iface);CFT_CHECK(cft_f[i].removed==1);}
    for(i=0;i<ARRAY_SIZE(cft_p);i++)CFT_CHECK(cft_p[i].refs==1);
    for(i=0;i<ARRAY_SIZE(cft_f);i++)CFT_CHECK(cft_f[i].refs==(i==2?5:3));
    CFT_CHECK(cft_freed==5 && !cft_error && !cf_devices && !cf_jobs && !cf_pumping);
    return 0;
}
