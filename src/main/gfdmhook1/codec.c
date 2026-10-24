#include <windows.h>
#include <objbase.h>

#include <stdbool.h>
#include <string.h>
#include <wchar.h>

#include "gfdmhook1/codec.h"
#include "hook/table.h"
#include "util/defs.h"
#include "util/log.h"

/* V4-V6 request the first class. V7/V8 try the second, then the first.
   Keep the requested class identity: different CLVSD releases expose different
   classes, and a matching 32-bit decoder must be supplied by the user. */
static const CLSID gfdm_codec_classes[] = {
    {0x8acd52ed, 0x9c2d, 0x4008,
     {0x91, 0x29, 0xdc, 0xe9, 0x55, 0xd8, 0x60, 0x65}},
    {0x37991d68, 0x42a3, 0x40e3,
     {0x8c, 0x05, 0x03, 0x71, 0x70, 0xe1, 0xa4, 0x2a}},
};
static const IID gfdm_codec_factory_iid = {
    0x00000001, 0x0000, 0x0000,
    {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46},
};

typedef HRESULT (WINAPI *gfdm_get_class_object_t)(REFCLSID, REFIID, void **);

static HRESULT (WINAPI *real_CoCreateInstance)(
    REFCLSID, IUnknown *, DWORD, REFIID, void **);
static CRITICAL_SECTION gfdm_codec_lock;
static wchar_t gfdm_codec_path[MAX_PATH];
static bool gfdm_codec_initialized;
static bool gfdm_codec_attempted;
static bool gfdm_codec_created[2];
static HMODULE gfdm_codec_module;
static gfdm_get_class_object_t gfdm_codec_get_class_object;

static gfdm_get_class_object_t gfdm_codec_load(void)
{
    gfdm_get_class_object_t get_class_object;
    DWORD attributes;
    DWORD error;

    EnterCriticalSection(&gfdm_codec_lock);
    if (!gfdm_codec_attempted) {
        gfdm_codec_attempted = true;
        attributes = GetFileAttributesW(gfdm_codec_path);
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
                log_info("GFDM tools/CLVSD.ax not found; using registered decoder");
            } else {
                log_warning("GFDM cannot access tools/CLVSD.ax (error %lu)", error);
            }
        } else if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            /* An absolute path anchors the AX and its dependencies beside the
               executable even if a launcher uses a different working directory.
               No DllRegisterServer call or machine-wide COM change is needed. */
            gfdm_codec_module = LoadLibraryExW(
                gfdm_codec_path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (gfdm_codec_module != NULL) {
                gfdm_codec_get_class_object = (gfdm_get_class_object_t)
                    GetProcAddress(gfdm_codec_module, "DllGetClassObject");
                if (gfdm_codec_get_class_object == NULL) {
                    log_warning("GFDM tools/CLVSD.ax has no DllGetClassObject");
                }
            } else {
                log_warning("GFDM cannot load tools/CLVSD.ax (error %lu); "
                            "using registered decoder", GetLastError());
            }
            /* Retain the loader reference for the process lifetime. Native
               libmovie calls CoFreeUnusedLibraries while graphs are reused. */
        } else {
            log_warning("GFDM tools/CLVSD.ax is a directory; using registered decoder");
        }
    }
    get_class_object = gfdm_codec_get_class_object;
    LeaveCriticalSection(&gfdm_codec_lock);
    return get_class_object;
}

static HRESULT WINAPI gfdm_CoCreateInstance(
    REFCLSID clsid, IUnknown *outer, DWORD context, REFIID iid, void **object)
{
    gfdm_get_class_object_t get_class_object;
    IClassFactory *factory;
    HRESULT hr;

    for (size_t i = 0; i < lengthof(gfdm_codec_classes); i++) {
        if (memcmp(clsid, &gfdm_codec_classes[i], sizeof(CLSID)) != 0 ||
            !(context & CLSCTX_INPROC_SERVER)) {
            continue;
        }
        if (object == NULL) {
            return E_POINTER;
        }
        get_class_object = gfdm_codec_load();
        if (get_class_object == NULL) {
            break;
        }
        *object = NULL;
        factory = NULL;
        hr = get_class_object(clsid, &gfdm_codec_factory_iid, (void **) &factory);
        if (SUCCEEDED(hr) && factory != NULL) {
            hr = IClassFactory_CreateInstance(factory, outer, iid, object);
            IClassFactory_Release(factory);
            if (SUCCEEDED(hr)) {
                EnterCriticalSection(&gfdm_codec_lock);
                if (!gfdm_codec_created[i]) {
                    gfdm_codec_created[i] = true;
                    log_info("GFDM tools/CLVSD.ax decoder class %u created "
                             "without registration", (unsigned int) i);
                }
                LeaveCriticalSection(&gfdm_codec_lock);
                return hr;
            }
            log_warning("GFDM tools/CLVSD.ax class %u creation failed "
                        "(hr=%08lx); using registered decoder",
                        (unsigned int) i, (unsigned long) hr);
        } else if (hr != CLASS_E_CLASSNOTAVAILABLE) {
            log_warning("GFDM tools/CLVSD.ax class %u factory failed "
                        "(hr=%08lx); using registered decoder",
                        (unsigned int) i, (unsigned long) hr);
        } else {
            log_misc("GFDM tools/CLVSD.ax does not provide decoder class %u; "
                     "trying registered/native decoder fallback", (unsigned int) i);
        }
        /* Preserve V7/V8's native newer-class -> older-class fallback, and
           keep existing registered installations usable if no local class fits. */
        break;
    }
    return real_CoCreateInstance(clsid, outer, context, iid, object);
}

static const struct hook_symbol gfdm_codec_syms[] = {
    {
        .name = "CoCreateInstance",
        .patch = gfdm_CoCreateInstance,
        .link = (void **) &real_CoCreateInstance,
    },
};

void gfdm_codec_init(void)
{
    DWORD length;
    wchar_t *slash;

    if (gfdm_codec_initialized) {
        return;
    }
    length = GetModuleFileNameW(NULL, gfdm_codec_path, lengthof(gfdm_codec_path));
    if (length == 0 || length >= lengthof(gfdm_codec_path)) {
        log_warning("GFDM cannot resolve executable directory for tools/CLVSD.ax");
        return;
    }
    slash = wcsrchr(gfdm_codec_path, L'\\');
    if (slash == NULL || (size_t) (slash + 1 - gfdm_codec_path) +
            lengthof(L"tools\\CLVSD.ax") > lengthof(gfdm_codec_path)) {
        log_warning("GFDM tools/CLVSD.ax path is too long");
        return;
    }
    wcscpy(slash + 1, L"tools\\CLVSD.ax");
    InitializeCriticalSection(&gfdm_codec_lock);
    gfdm_codec_initialized = true;
    gfdm_codec_apply_hooks();
}

void gfdm_codec_apply_hooks(void)
{
    HMODULE movie;

    if (!gfdm_codec_initialized) {
        return;
    }
    movie = GetModuleHandleW(L"libmovie.dll");
    if (movie != NULL) {
        /* Scope this to the verified native movie consumer, not all COM users
           or quartz's internal class activation. Reapply after late loading. */
        hook_table_apply(movie, "ole32.dll", gfdm_codec_syms,
                         lengthof(gfdm_codec_syms));
    }
}
