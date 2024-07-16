#ifndef memDisplay_h
#define memDisplay_h

#include <stdio.h>

#include "shareLib.h"

#ifdef __cplusplus
extern "C" {
#endif

epicsShareExtern int memDisplayDebug;

epicsShareFunc int memDisplay(size_t base, volatile void* ptr, int wordsize, size_t bytes);
epicsShareFunc int fmemDisplay(FILE* outfile, size_t base, volatile void* ptr, int wordsize, size_t bytes);
#define memDisplay(base, ptr, wordsize, bytes) fmemDisplay(stdout, base, ptr, wordsize, bytes)

typedef volatile void* (*memDisplayAddrHandler) (size_t addr, size_t size, size_t usr);
epicsShareFunc void memDisplayInstallAddrHandler(const char* str, memDisplayAddrHandler handler, size_t usr);

typedef volatile void* (*memDisplayAddrTranslator) (const char* addr, size_t offs, size_t size);
epicsShareFunc void memDisplayInstallAddrTranslator(memDisplayAddrTranslator handler);

epicsShareFunc unsigned long long strToSize(const char* str, char** endptr);
epicsShareFunc char* sizeToStr(unsigned long long size, char* str);
epicsShareFunc volatile void* strToPtr(const char* addrstr, size_t size);

epicsShareFunc int memfill(volatile void* address, int pattern, size_t size, int wordsize, int increment);
epicsShareFunc int memcopy(const volatile void* source, volatile void* dest, size_t size, int wordsize);
epicsShareFunc int memcomp(const volatile void* source, const volatile void* dest, size_t size, int wordsize);

#ifdef __cplusplus
}
#endif
#endif
