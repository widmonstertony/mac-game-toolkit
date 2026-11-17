/* LGPL-2.1-or-later. Included in Wine windows.gaming.input/manager.c.
 * Registry owns factories for process lifetime (the API has no unregister).
 * Device callbacks are serialized by a queue and never run under cf_lock.
 */
enum cf_kind { CF_HARDWARE, CF_GIP };
struct cf_registration
{
    struct cf_registration *next;
    ICustomGameControllerFactory *factory;
    IUnknown *identity;
    enum cf_kind kind;
    UINT16 vendor, product;
    GUID gip;
};
struct cf_device;
struct cf_binding;
struct cf_job { struct cf_job *next; struct cf_binding *binding; BOOL remove; };
struct cf_binding
{
    struct cf_binding *next;
    struct cf_device *device;
    struct cf_registration *registration;
    struct controller *controller;
    struct cf_job add_job, remove_job;
};
struct cf_device
{
    struct cf_device *next;
    IGameControllerProvider *provider;
    LONG refs;
    BOOL hardware_valid, has_gip;
    UINT16 vendor, product;
    GUID gip;
    struct cf_binding *bindings;
};
static SRWLOCK cf_lock = SRWLOCK_INIT;
static struct cf_registration *cf_registrations;
static struct cf_device *cf_devices;
static struct cf_job *cf_jobs, **cf_jobs_tail = &cf_jobs;
static BOOL cf_pumping;

static void cf_device_release(struct cf_device *device)
{
    if (!InterlockedDecrement(&device->refs))
    {
        IGameControllerProvider_Release(device->provider);
        free(device);
    }
}

static void cf_enqueue(struct cf_job *job)
{
    job->next = NULL;
    *cf_jobs_tail = job;
    cf_jobs_tail = &job->next;
}

static void cf_pump(void)
{
    struct cf_job *job;
    struct cf_binding *binding;
    struct controller *controller;
    HRESULT hr;
    AcquireSRWLockExclusive(&cf_lock);
    if (cf_pumping) { ReleaseSRWLockExclusive(&cf_lock); return; }
    cf_pumping = TRUE;
    for (;;)
    {
        if (!(job = cf_jobs)) break;
        if (!(cf_jobs = job->next)) cf_jobs_tail = &cf_jobs;
        binding = job->binding;
        ReleaseSRWLockExclusive(&cf_lock);
        if (!job->remove)
        {
            hr = controller_create_external(binding->registration->factory,
                                             binding->device->provider, &controller);
            if (SUCCEEDED(hr) && controller)
            {
                binding->controller = controller;
                EnterCriticalSection(&manager_cs);
                list_add_tail(&controller_list, &controller->entry);
                LeaveCriticalSection(&manager_cs);
                hr = ICustomGameControllerFactory_OnGameControllerAdded(controller->factory,
                                                                         &controller->IGameController_iface);
                if (FAILED(hr)) WARN("Custom controller added callback failed %#lx\n", hr);
            }
            else if (FAILED(hr)) WARN("Custom controller creation failed %#lx\n", hr);
        }
        else
        {
            if ((controller = binding->controller))
            {
                EnterCriticalSection(&manager_cs);
                list_remove(&controller->entry);
                LeaveCriticalSection(&manager_cs);
                ICustomGameControllerFactory_OnGameControllerRemoved(controller->factory,
                                                                      &controller->IGameController_iface);
                IGameController_Release(&controller->IGameController_iface);
            }
            cf_device_release(binding->device);
            free(binding);
        }
        AcquireSRWLockExclusive(&cf_lock);
    }
    cf_pumping = FALSE;
    ReleaseSRWLockExclusive(&cf_lock);
}

static BOOL cf_matches(const struct cf_registration *r, const struct cf_device *d)
{
    if (r->kind == CF_HARDWARE)
        return d->hardware_valid && r->vendor == d->vendor && r->product == d->product;
    return d->has_gip && IsEqualGUID(&r->gip, &d->gip);
}

static BOOL cf_bound(const struct cf_device *d, const struct cf_registration *r)
{
    const struct cf_binding *b;
    for (b = d->bindings; b; b = b->next)
        if (b->registration->identity == r->identity) return TRUE;
    return FALSE;
}

static struct cf_binding *cf_new_binding(struct cf_device *d, struct cf_registration *r)
{
    struct cf_binding *b = calloc(1, sizeof(*b));
    if (!b) return NULL;
    b->device = d; b->registration = r;
    b->add_job.binding = b; b->remove_job.binding = b; b->remove_job.remove = TRUE;
    InterlockedIncrement(&d->refs);
    return b;
}

static void cf_attach(struct cf_binding *b)
{
    b->next = b->device->bindings;
    b->device->bindings = b;
    cf_enqueue(&b->add_job);
}

static HRESULT cf_register(ICustomGameControllerFactory *factory, enum cf_kind kind,
                           UINT16 vendor, UINT16 product, const GUID *gip)
{
    struct cf_registration *r, *old;
    struct cf_device *d;
    struct cf_binding *pending = NULL, *b, *next;
    HRESULT hr;
    if (!factory) return E_INVALIDARG;
    if (!(r = calloc(1, sizeof(*r)))) return E_OUTOFMEMORY;
    hr = ICustomGameControllerFactory_QueryInterface(factory, &IID_IUnknown, (void **)&r->identity);
    if (FAILED(hr)) { free(r); return hr; }
    ICustomGameControllerFactory_AddRef((r->factory = factory));
    r->kind = kind; r->vendor = vendor; r->product = product;
    if (gip) r->gip = *gip;
    AcquireSRWLockExclusive(&cf_lock);
    for (old = cf_registrations; old; old = old->next)
        if (old->identity == r->identity && old->kind == kind &&
            (kind == CF_HARDWARE ? old->vendor == vendor && old->product == product : IsEqualGUID(&old->gip, gip)))
            goto duplicate;
    /* Allocate the entire transaction before publishing a registration. */
    for (d = cf_devices; d; d = d->next)
    {
        if (!cf_matches(r, d) || cf_bound(d, r)) continue;
        if (!(b = cf_new_binding(d, r)))
        {
            for (b = pending; b; b = next)
            { next = b->next; InterlockedDecrement(&b->device->refs); free(b); }
            hr = E_OUTOFMEMORY;
            goto duplicate;
        }
        b->next = pending; pending = b;
    }
    r->next = cf_registrations; cf_registrations = r;
    for (b = pending; b; b = next) { next = b->next; cf_attach(b); }
    ReleaseSRWLockExclusive(&cf_lock);
    cf_pump();
    return S_OK;
duplicate:
    ReleaseSRWLockExclusive(&cf_lock);
    ICustomGameControllerFactory_Release(r->factory);
    IUnknown_Release(r->identity); free(r);
    return hr;
}

/* Current Wine HID/XInput providers have no GIP identity. NULL means no GIP
 * match, never an invented COM IID. A GIP transport must supply its actual ID. */
static void cf_provider_created(IGameControllerProvider *provider, const GUID *gip)
{
    struct cf_device *d, *old;
    struct cf_registration *r;
    struct cf_binding *b;
    if (!(d = calloc(1, sizeof(*d)))) return;
    d->refs = 1; IGameControllerProvider_AddRef((d->provider = provider));
    d->hardware_valid = SUCCEEDED(IGameControllerProvider_get_HardwareVendorId(provider, &d->vendor)) &&
                        SUCCEEDED(IGameControllerProvider_get_HardwareProductId(provider, &d->product));
    if (gip) { d->has_gip = TRUE; d->gip = *gip; }
    AcquireSRWLockExclusive(&cf_lock);
    for (old = cf_devices; old; old = old->next)
        if (old->provider == provider)
        { ReleaseSRWLockExclusive(&cf_lock); cf_device_release(d); return; }
    d->next = cf_devices; cf_devices = d;
    for (r = cf_registrations; r; r = r->next)
    {
        if (!cf_matches(r, d) || cf_bound(d, r)) continue;
        if ((b = cf_new_binding(d, r))) cf_attach(b);
        else WARN("Cannot allocate custom controller binding\n");
    }
    ReleaseSRWLockExclusive(&cf_lock);
    cf_pump();
}

static void cf_provider_removed(IGameControllerProvider *provider)
{
    struct cf_device **link, *d = NULL;
    struct cf_binding *b;
    AcquireSRWLockExclusive(&cf_lock);
    for (link = &cf_devices; *link; link = &(*link)->next)
        if ((*link)->provider == provider) { d = *link; *link = d->next; break; }
    if (d)
        for (b = d->bindings; b; b = b->next) cf_enqueue(&b->remove_job);
    ReleaseSRWLockExclusive(&cf_lock);
    if (d) cf_device_release(d);
    cf_pump();
}

static HRESULT cf_register_hardware(ICustomGameControllerFactory *factory, UINT16 vendor, UINT16 product)
{ return cf_register(factory, CF_HARDWARE, vendor, product, NULL); }
static HRESULT cf_register_gip(ICustomGameControllerFactory *factory, const GUID *gip)
{ return cf_register(factory, CF_GIP, 0, 0, gip); }
