#include "ms/dynlib.h"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>

MsDynlib ms_dynlib_open(const char* p) {
    return (MsDynlib)LoadLibraryA(p);
}

void* ms_dynlib_sym(MsDynlib l, const char* n) {
    return (void*)(uintptr_t)GetProcAddress((HMODULE)l, n);
}

void ms_dynlib_close(MsDynlib l) {
    if (l) FreeLibrary((HMODULE)l);
}

#else
#  include <dlfcn.h>

MsDynlib ms_dynlib_open(const char* p) {
    return dlopen(p, RTLD_NOW | RTLD_LOCAL);
}

void* ms_dynlib_sym(MsDynlib l, const char* n) {
    return dlsym(l, n);
}

void ms_dynlib_close(MsDynlib l) {
    if (l) dlclose(l);
}

#endif
