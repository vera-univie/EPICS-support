#ifdef _WIN32
#include <Windows.h>
#include <memoryapi.h>
#define valloc(size) VirtualAlloc(NULL, size, MEM_COMMIT, PAGE_READWRITE)
#endif

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <epicsString.h>

#include <epicsStdioRedirect.h>
#include <devLib.h>
#include <envDefs.h>
#include <iocsh.h>
#include <epicsFindSymbol.h>

#ifdef vxWorks
#include <memLib.h>
#endif

#include "epicsExport.h"
#include "memDisplay.h"

struct addressHandlerItem {
    const char* name;
    memDisplayAddrHandler handler;
    size_t usr;
    struct addressHandlerItem* next;
} *addressHandlerList = NULL;

void memDisplayInstallAddrHandler(const char* name, memDisplayAddrHandler handler, size_t usr)
{
    char *s;
    struct addressHandlerItem* item =
        (struct addressHandlerItem*) malloc(sizeof(struct addressHandlerItem));
    if (!item)
    {
        printf("Out of memory.\n");
        return;
    }
    if (!name)
    {
        printf("Missing name.\n");
        return;
    }
    s = malloc(strlen(name)+1);
    strcpy(s, name);
    item->name = s;
    item->handler = handler;
    item->usr = usr;
    item->next = addressHandlerList;
    addressHandlerList = item;
}

struct addressTranslatorItem {
    memDisplayAddrTranslator translator;
    struct addressTranslatorItem* next;
} *addressTranslatorList = NULL;

void memDisplayInstallAddrTranslator(memDisplayAddrTranslator translator)
{
    struct addressTranslatorItem* item =
        (struct addressTranslatorItem*) malloc(sizeof(struct addressTranslatorItem));
    if (!item)
    {
        printf("Out of memory.\n");
        return;
    }
    item->translator = translator;
    item->next = addressTranslatorList;
    addressTranslatorList = item;
}

typedef struct {volatile void* ptr; size_t offs;} remote_addr_t;
static remote_addr_t strToAddr(const char* addrstr, size_t offs, size_t size)
{
    unsigned long long addr = 0;
    size_t len;
    volatile char* ptr = NULL;
    struct addressHandlerItem* hitem;
    struct addressTranslatorItem* titem;
    char *p, *q;
    char c;

    for (hitem = addressHandlerList; hitem != NULL; hitem = hitem->next)
    {
        len = strlen(hitem->name);
        if (strncmp(addrstr, hitem->name, len) == 0 &&
            (addrstr[len] == 0 || addrstr[len] == ':'))
        {
            if (addrstr[len])
            {
                addr = strToSize(addrstr+len+1, &q) + offs;
                if (*q != 0)
                {
                    /* rubbish at end */
                    fprintf(stderr, "Invalid address %s.\n", addrstr);
                    return (remote_addr_t){NULL, 0};
                }
                if (addr & ~(unsigned long long)((size_t)-1))
                {
                    fprintf(stderr, "Too large address %s for %u bit.\n", addrstr, (int) sizeof(void*)*8);
                    return (remote_addr_t){NULL, 0};
                }
            }
            errno = 0;
            ptr = hitem->handler(addr, size, hitem->usr);
            if (!ptr)
            {
                if (errno)
                    fprintf(stderr, "Getting address 0x%llx in %s address space failed: %s\n",
                        addr, hitem->name, strerror(errno));
                else
                    fprintf(stderr, "Getting address 0x%llx in %s address space failed.\n",
                        addr, hitem->name);
            }
            return (remote_addr_t){ptr, addr};
        }
    }
    for (titem = addressTranslatorList; titem != NULL; titem = titem->next)
    {
        if ((p = strrchr((char*)addrstr, ':')) != NULL)
        {
            addr = strToSize(p+1, &q);
        }
        ptr = titem->translator(addrstr, offs, size);
        if (ptr) return (remote_addr_t){ptr, addr + offs};
    }

    /* no addrspace */
    if (!addr && (ptr = epicsFindSymbol(addrstr)) != NULL)
    {
        /* global variable name */
        return (remote_addr_t){ptr + offs, (size_t)ptr + offs};
    }
    if (sscanf(addrstr, "%p%c", &ptr, &c) == 1) {
        ptr += offs;
        return (remote_addr_t){ptr + offs, (size_t)ptr + offs};
    }
    addr = strToSize(addrstr, &q) + offs;
    if (q > addrstr)
    {
        /* something like a number */
        if (*q != 0)
        {
            /* rubbish at end */
            fprintf(stderr, "Unparsable address %s\n", addrstr);
            return (remote_addr_t){NULL, 0};
        }
        if (addr & ~(unsigned long long)((size_t)-1))
        {
            fprintf(stderr, "Too large address %s for %u bit.\n", addrstr, (int) sizeof(void*)*8);
            return (remote_addr_t){NULL, 0};
        }
        return (remote_addr_t){(void*)(size_t) addr, addr};
    }
    fprintf(stderr, "Unknown address %s\n", addrstr);
    return (remote_addr_t){NULL, 0};
}

volatile void* strToPtr(const char* addrStr, size_t size)
{
    remote_addr_t addr = strToAddr(addrStr, 0, size);
    return addr.ptr;
}

void md(const char* addrStr, int wordsize, int bytes)
{
    remote_addr_t addr;
    static remote_addr_t old_addr = {0};
    static int old_wordsize = 2;
    static int old_bytes = 0x80;
    static char* old_addrStr;
    static size_t old_offs;

    if ((!addrStr && !old_addr.ptr) || (addrStr && addrStr[0] == '?'))
    {
        printf("md \"[addrspace:]address\", [wordsize={1|2|4|8|-2|-4|-8}], [bytes]\n");
        return;
    }
    if (addrStr)
    {
        free(old_addrStr);
        old_addrStr = epicsStrDup(addrStr);
        old_offs = 0;
        old_wordsize = 2;
    }
    else
    {
        addrStr = old_addrStr;
    }
    if (bytes == 0) bytes = old_bytes;
    if (wordsize == 0) wordsize = old_wordsize;
    addr = strToAddr(addrStr, old_offs, bytes);
    if (!addr.ptr)
        return;
    if (memDisplay(addr.offs, addr.ptr, wordsize, bytes) < 0)
    {
        old_addr = (remote_addr_t){0};
        return;
    }
    old_offs += bytes;
    old_wordsize = wordsize;
    old_bytes = bytes;
    old_addr = addr;
}

epicsExportAddress(int, memDisplayDebug);

static const iocshArg mdArg0 = { "[addrspace:]address", iocshArgString };
static const iocshArg mdArg1 = { "[wordsize={1|2|4|8|-2|-4|-8}]", iocshArgInt };
static const iocshArg mdArg2 = { "[bytes]", iocshArgInt };
static const iocshArg *mdArgs[] = {&mdArg0, &mdArg1, &mdArg2};
static const iocshFuncDef mdDef = { "md", 3, mdArgs };

static void mdFunc(const iocshArgBuf *args)
{
    md(args[0].sval, args[1].ival, args[2].ival);
}

static const iocshFuncDef mallocDef =
    { "malloc", 1, (const iocshArg *[]) {
    &(iocshArg) { "size", iocshArgString },
}};

static void mallocFunc(const iocshArgBuf *args)
{
    void *p;
    char b[20];
    p = valloc(strToSize(args[0].sval, NULL));
    sprintf(b, "%p", p);
    epicsEnvSet("BUFFER", b);
    printf("BUFFER = %s\n", b);
}

static const iocshFuncDef memfillDef =
    { "memfill", 5, (const iocshArg *[]) {
    &(iocshArg) { "[addrspace:]address", iocshArgString },
    &(iocshArg) { "pattern", iocshArgInt },
    &(iocshArg) { "size", iocshArgString },
    &(iocshArg) { "wordsize", iocshArgInt },
    &(iocshArg) { "increment", iocshArgInt },
}};

static void memfillFunc(const iocshArgBuf *args)
{
    int pattern;
    size_t size;
    int wordsize;
    int increment;
    volatile void* address;

    if (!args[0].sval)
    {
        iocshCmd("help memfill");
        return;

    }

    size = strToSize(args[2].sval, NULL);
    address = strToPtr(args[0].sval, size);
    if (!address)
    {
        fprintf(stderr, "Cannot map address %s\n", args[0].sval);
        return;
    }

    pattern = args[1].ival;
    wordsize = args[3].ival;
    increment = args[4].ival;
    memfill(address, pattern, size, wordsize, increment);
}

static const iocshFuncDef memcopyDef =
    { "memcopy", 4, (const iocshArg *[]) {
    &(iocshArg) { "[addrspace:]source", iocshArgString },
    &(iocshArg) { "[addrspace:]dest", iocshArgString },
    &(iocshArg) { "size", iocshArgString },
    &(iocshArg) { "wordsize", iocshArgInt },
}};

void memcopyFunc(const iocshArgBuf *args)
{
    volatile void* source;
    volatile void* dest;
    size_t size;
    int wordsize;

    if (!args[0].sval || !args[1].sval || !args[2].sval)
    {
        iocshCmd("help memcopy");
        return;
    }

    size = strToSize(args[2].sval, NULL);
    source = strToPtr(args[0].sval, size);
    if (!source)
    {
        fprintf(stderr, "Cannot map source address %s\n", args[0].sval);
        return;
    }

    dest = strToPtr(args[1].sval, size);
    if (!dest)
    {
        fprintf(stderr, "Cannot map dest address %s\n", args[1].sval);
        return;
    }

    wordsize = args[3].ival;
    memcopy(source, dest, size, wordsize);
}

static const iocshFuncDef memcompDef =
    { "memcomp", 4, (const iocshArg *[]) {
    &(iocshArg) { "[addrspace:]source", iocshArgString },
    &(iocshArg) { "[addrspace:]dest", iocshArgString },
    &(iocshArg) { "size", iocshArgString },
    &(iocshArg) { "wordsize", iocshArgInt },
}};

static void memcompFunc(const iocshArgBuf *args)
{
    volatile void* source;
    volatile void* dest;
    size_t size;
    int wordsize;

    if (!args[0].sval || !args[1].sval || !args[2].sval)
    {
        iocshCmd("help memcomp");
        return;
    }

    size = strToSize(args[2].sval, NULL);
    source = strToPtr(args[0].sval, size);
    if (!source)
    {
        fprintf(stderr, "Cannot map source address %s\n", args[0].sval);
        return;
    }

    dest = strToPtr(args[1].sval, size);
    if (!dest)
    {
        fprintf(stderr, "Cannot map dest address %s\n", args[1].sval);
        return;
    }

    wordsize = args[3].ival;
    memcomp(source, dest, size, wordsize);
}

static void memDisplayRegistrar(void)
{
    iocshRegister(&mdDef, mdFunc);
    iocshRegister(&mallocDef, mallocFunc);
    iocshRegister(&memfillDef, memfillFunc);
    iocshRegister(&memcopyDef, memcopyFunc);
    iocshRegister(&memcompDef, memcompFunc);
}
epicsExportRegistrar(memDisplayRegistrar);
