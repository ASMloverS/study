#pragma once

/* Cross-platform thin wrapper around dlopen/LoadLibrary (CAPI-04). */

typedef void* MsDynlib;

/* Open a shared library.  Returns NULL on failure. */
MsDynlib  ms_dynlib_open(const char* path);

/* Look up a symbol in a library.  Returns NULL if not found. */
void*     ms_dynlib_sym(MsDynlib lib, const char* name);

/* Close a library handle. */
void      ms_dynlib_close(MsDynlib lib);
