#include "gfdmhook1/mods.h"
#if GFDM_MOD
/* Independent whole-file overlay. Search layout/priority reviewed against
   mon/ifs_layeredfs a0312f8; shared-data manifests are a separate concern. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfdmhook1/mods.h"
#include "gfdmhook1/mod-codec.h"
#include "hook/table.h"
#include "imports/avs.h"
#include "util/log.h"

#define MODS_MOUNT "/__gfdm_mods"
static char mods_root[MAX_PATH];
static bool mods_enabled, mods_mounted;
static int (__cdecl *mods_real_mount)(const char *, const char *, const char *, void *);
static int (__cdecl *mods_real_lstat)(const char *, struct avs_stat *);
static int (__cdecl *mods_real_convert)(char *, const char *);
static int (*mods_base_reader)(const char *, unsigned char **, size_t *);
static __thread bool mods_processing;

void gfdm_mods_set_base_reader(int (*reader)(const char *, unsigned char **, size_t *))
{ mods_base_reader=reader; }

static bool mods_relative(const char *name, char relative[MAX_PATH])
{
    if (!name) return false;
    if (!strncmp(name, "./", 2)) name += 2;
    else if (!strncmp(name, "../", 3)) name += 3;
    if (*name == '/' || *name == '\\') name++;
    if (_strnicmp(name, "data/", 5) && _strnicmp(name, "data\\", 5)) return false;
    name += 5;
    size_t length = strlen(name);
    if (!length || length >= MAX_PATH) return false;
    for (size_t i = 0; i <= length; i++)
        relative[i] = name[i] == '\\' ? '/' : name[i];
    /* Metadata and backing objects belong to the verified base store. */
    if (!_strnicmp(relative, "_loose/", 7) ||
        !_strnicmp(relative, "pack/", 5) ||
        !_stricmp(relative, "finfolist.bin")) return false;
    char *segment = relative;
    for (char *p = relative;; p++) {
        if (*p == ':' || *p == '*' || *p == '?' || *p == '"') return false;
        if (*p == '/' || !*p) {
            size_t n = (size_t)(p - segment);
            if (!n || (n == 1 && segment[0] == '.') ||
                (n == 2 && segment[0] == '.' && segment[1] == '.')) return false;
            if (!*p) break;
            segment = p + 1;
        }
    }
    return true;
}

bool gfdm_mods_init(bool enabled)
{
    mods_enabled = false;
    mods_mounted = false;
    if (!enabled) return true;
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe, sizeof(exe));
    if (!n || n >= sizeof(exe)) return false;
    char *slash = strrchr(exe, '\\');
    if (!slash) return false;
    *slash = 0;
    if (snprintf(mods_root, sizeof(mods_root), "%s/data_mods", exe) >= sizeof(mods_root)) return false;
    DWORD attr = GetFileAttributesA(mods_root);
    mods_enabled = attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
    mods_mounted = false;
    if (!mods_enabled) return true;
    HMODULE avs = GetModuleHandleA("libavs-win32.dll");
    mods_real_mount = (void *) GetProcAddress(avs, "avs_fs_mount");
    mods_real_lstat = (void *) GetProcAddress(avs, "avs_fs_lstat");
    mods_real_convert = (void *) GetProcAddress(avs, "avs_fs_convert_path");
    if (!mods_real_mount || !mods_real_lstat || !mods_real_convert) return false;
    log_info("GFDM data_mods: %s (independent of shared data)", mods_root);
    return true;
}

bool gfdm_mods_enabled(void) { return mods_enabled; }

bool gfdm_mods_mount(void)
{
    if (!mods_enabled) return true;
    mods_mounted = mods_real_mount(MODS_MOUNT, mods_root, "fs", NULL) >= 0;
    return mods_mounted;
}

static bool mods_resolve_file(const char *name, char *output, size_t capacity)
{
    char relative[MAX_PATH], search[MAX_PATH], candidate[MAX_PATH];
    char selected[MAX_PATH] = "";
    WIN32_FIND_DATAA item;
    if (!mods_enabled || !mods_mounted || !mods_relative(name, relative)) return false;
    if (snprintf(search, sizeof(search), "%s/*", mods_root) >= sizeof(search)) return false;
    HANDLE find = FindFirstFileA(search, &item);
    if (find == INVALID_HANDLE_VALUE) return false;
    do {
        if (!(item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (item.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            !strcmp(item.cFileName, ".") || !strcmp(item.cFileName, "..") ||
            !_stricmp(item.cFileName, "_cache")) continue;
        if (selected[0] && _stricmp(selected, item.cFileName) <= 0) continue;
        int n = snprintf(candidate, sizeof(candidate), "%s/%s/%s", mods_root, item.cFileName, relative);
        if (n < 0 || n >= sizeof(candidate)) continue;
        DWORD attr = GetFileAttributesA(candidate);
        if (attr != INVALID_FILE_ATTRIBUTES &&
            !(attr & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
            strcpy(selected, item.cFileName);
    } while (FindNextFileA(find, &item));
    FindClose(find);
    if (!selected[0]) return false;
    int n = snprintf(output, capacity, MODS_MOUNT "/%s/%s", selected, relative);
    return n >= 0 && (size_t)n < capacity;
}

static int mods_read_base(void *context, unsigned char **data, size_t *size)
{ return mods_base_reader ? mods_base_reader(context,data,size) : -1; }

struct mods_xml_reader { const unsigned char *data; size_t size, offset; };
static int mods_xml_read(uint32_t context, void *buffer, uint32_t count)
{
    struct mods_xml_reader *r=(void *)(uintptr_t)context;
    size_t available=r->size-r->offset;if(available>count)available=count;
    memcpy(buffer,r->data+r->offset,available);r->offset+=available;return (int)available;
}
static int mods_xml_text(const unsigned char *input,size_t size,unsigned char **output,size_t *length)
{
    HMODULE avs=GetModuleHandleA("libavs-win32.dll");
    uint32_t (*query)(avs_reader_t,uint32_t,int,int)=(void *)GetProcAddress(avs,"property_read_query_memsize");
    struct property *(*create)(int,void *,uint32_t)=(void *)GetProcAddress(avs,"property_create");
    int (*insert)(struct property *,struct property_node *,avs_reader_t,uint32_t)=(void *)GetProcAddress(avs,"property_insert_read");
    int (*query_out)(struct property *)=(void *)GetProcAddress(avs,"property_query_size");
    int (*write)(struct property *,void *,int)=(void *)GetProcAddress(avs,"property_mem_write");
    int (*set_flag)(struct property *,int,int)=(void *)GetProcAddress(avs,"property_set_flag");
    void (*destroy)(struct property *)=(void *)GetProcAddress(avs,"property_destroy");
    if(!query||!create||!insert||!query_out||!write||!destroy)return 0;
    struct mods_xml_reader r={input,size,0};
    uint32_t capacity=query(mods_xml_read,(uint32_t)(uintptr_t)&r,0,0);
    if((int32_t)capacity<=0||capacity>128*1024*1024)return 0;
    void *memory=calloc(1,capacity);if(!memory)return 0;
    struct property *p=create(0x17,memory,capacity);
    unsigned char *text=NULL;int count=0;
    r.offset=0;
    if(p&&insert(p,NULL,mods_xml_read,(uint32_t)(uintptr_t)&r)>0) {
        if(set_flag)set_flag(p,0,PROPERTY_FLAG_BINARY);
        count=query_out(p);
        if(count>0&&count<128*1024*1024) {
            text=malloc((size_t)count+1);
            if(text) {count=write(p,text,count);if(count>0)text[count]=0;}
        }
    }
    if(p)destroy(p);free(memory);
    if(!text||count<=0){free(text);return 0;}
    *output=text;*length=(size_t)count;return 1;
}
static void mods_codec_log(int level,const char *message)
{
    if(level<=1)log_warning("GFDM data_mods converter: %s",message);
    else log_misc("GFDM data_mods converter: %s",message);
}

bool gfdm_mods_resolve(const char *name,char *output,size_t capacity)
{
    char relative[MAX_PATH],generated[MAX_PATH];
    if(!mods_enabled||!mods_mounted||mods_processing||!mods_relative(name,relative))return false;
    struct gfdm_mod_backend backend={(void *)name,mods_read_base,mods_xml_text,free,mods_codec_log};
    mods_processing=true;
    bool converted=gfdm_mod_convert(mods_root,relative,&backend,generated,sizeof(generated));
    mods_processing=false;
    if(converted) {
        size_t root_length=strlen(mods_root);
        if(!_strnicmp(generated,mods_root,root_length)&&(generated[root_length]=='/'||generated[root_length]=='\\')) {
            int n=snprintf(output,capacity,MODS_MOUNT "%s",generated+root_length);
            if(n>=0&&(size_t)n<capacity)
                for(char *p=output;*p;p++)if(*p=='\\')*p='/';
            return n>=0&&(size_t)n<capacity;
        }
        return false;
    }
    return mods_resolve_file(name,output,capacity);
}

static int __cdecl mods_lstat(const char *name, struct avs_stat *stat)
{
    char path[MAX_PATH];
    return mods_real_lstat(gfdm_mods_resolve(name, path, sizeof(path)) ? path : name, stat);
}

static int __cdecl mods_convert(char *output, const char *name)
{
    char path[MAX_PATH];
    return mods_real_convert(output, gfdm_mods_resolve(name, path, sizeof(path)) ? path : name);
}

void gfdm_mods_apply_hooks(void)
{
    static const struct hook_symbol symbols[] = {
        {.name="avs_fs_lstat", .patch=mods_lstat},
        {.name="avs_fs_convert_path", .patch=mods_convert},
    };
    if (mods_enabled) hook_table_apply(NULL, "libavs-win32.dll", symbols, 2);
}

#else
bool gfdm_mods_init(bool enabled) { (void)enabled; return true; }
bool gfdm_mods_enabled(void) { return false; }
bool gfdm_mods_mount(void) { return true; }
bool gfdm_mods_resolve(const char *name, char *output, size_t size)
{ (void)name; (void)output; (void)size; return false; }
void gfdm_mods_apply_hooks(void) {}
void gfdm_mods_set_base_reader(int (*reader)(const char *, unsigned char **, size_t *))
{ (void)reader; }
#endif
