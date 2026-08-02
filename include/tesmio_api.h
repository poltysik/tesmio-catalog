// Minimal TesmioLoader plugin ABI used by TesmioMenu.
// ABI version 3, copied from MaxLegend/TesmioLoader (GPL-3.0).
#ifndef TESMIO_API_H
#define TESMIO_API_H

#include <stddef.h>

#define TSM_API_VERSION 3u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TsmHost
{
    unsigned    apiVersion;
    unsigned    structSize;

    void*          exeModule;
    unsigned char* exeBase;
    size_t         exeSize;
    void*          engineModule;
    const char*    baseDir;
    const char*    pluginDir;

    void (*log)(const char* fmt, ...);

    void** (*findIatSlot)(void* module, const char* dll, const char* fn);
    int (*patchIat)(void* module, const char* dll, const char* fn,
                    void* detour, void** original, const char* label);
    int (*installInlineHook)(void* target, void* detour, void** trampoline,
                             const unsigned char* expect, size_t stolen,
                             const char* label);
    unsigned char* (*allocNear)(unsigned char* anchor, size_t size);
    int (*readablePtr)(const void* p, size_t n);
    long (*faultFilter)(const char* what, void* exceptionPointers);

    int (*configInt)(const char* iniName, const char* section,
                     const char* key, int fallback);
    int (*configString)(const char* iniName, const char* section,
                        const char* key, char* out, int outSize,
                        const char* fallback);

    int         (*provide)(const char* service, unsigned version, const void* iface);
    const void* (*consume)(const char* service, unsigned version);
} TsmHost;

typedef struct TsmPluginInfo
{
    const char* name;
    const char* version;
} TsmPluginInfo;

#ifdef __cplusplus
}
#endif

#endif
