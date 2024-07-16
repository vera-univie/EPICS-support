# Memory display utility

The function `memDisplay` can be used for hex + ASCII dump of
arbitrary memory regions. Access to invalid addresses it caught.

Output looks like this:

    7f11453f94d0: 8b48 b905 2010 4900 c889 d189 8948 48f2 H.... .I....H..H
    7f11453f94e0: fe89 8b48 e938 eb56 ffff 0f66 441f 0000 ..H.8.V...f..D..
    7f11453f94f0: 8348 08ec 8b48 9505 2010 4800 568b 4810 H...H.... .H.V.H
    7f11453f9500: 358d 08a5 0000 8b48 3138 e8c0 eb90 ffff .5....H.81......
    7f11453f9510: 8d48 a93d 2012 be00 0001 0000 3fe8 ffeb H.=.. .......?..
    7f11453f9520: 66ff 6666 6666 2e66 1f0f 0084 0000 0000 .ffffff.........
    7f11453f9530: 8948 246c 48d8 f589 8d48 8235 0008 4800 H.l$.H..H.5....H
    7f11453f9540: 5c89 d024 894c 2464 49e0 d489 894c 246c .\$.L.d$.I..L.l$

## memDisplay

    int memDisplay(size_t base, volatile void* ptr, int wordsize, size_t bytes);
    int fmemDisplay(FILE* outfile, size_t base, volatile void* ptr, int wordsize, size_t bytes);

Displays memory region starting at `ptr` of length `bytes` in hex and ASCII.
Output goes to `stdout` or the file `outfile`.
Lines of 16 bytes are prefixed with a hex address starting at `base`
(which may be different from `ptr`, for example for memory mapped devices).
The memory can be displayed as 1, 2, 4, or 8 (`wordsize`) bytes wide words.
If `wordsize` is negative, the words are displayed byte swapped.

A signal handler is active during the execution of `memDisplay` to catch any
access to invalid addresses so that the program will not crash.

## md

    md address wordsize bytes

This iocsh function calls memDisplay.
The `address` parameter can be a number (may be hex) to denote a
memory location, but it can also be the name of a global variable
(or other global symbol, e.g. a function).

Furthermore, `address` can be a number prefixed by an address space
name and a colon like `A16:`.
No address spaces are installed by default but other modules may
install address spaces (see below).

If `wordsize` or `bytes` is not specified, the prevous value is used,
starting with wordsize 2 and 64 bytes.

If `address` is not specified, the memory block directly following the
block of the prevoius call is displayed.

## memDisplayInstallAddrHandler

    typedef volatile void* (*memDisplayAddrHandler) (size_t addr, size_t size, size_t usr);
    void memDisplayInstallAddrHandler(const char* name, memDisplayAddrHandler handler, size_t usr);

This function can be used to install new handlers for the `address`
parameter of `md`.

The handler function is supposed to map an address region to a pointer.
The installer function binds the helper function to `name` so that
`name:` can be used as a prefix in the `address` parameter of `md`.
The parameter `usr` is an arbitrary value which the handler may use.
It is large enough to hold a pointer.

## memDisplayInstallAddrTranslator

    typedef volatile void* (*memDisplayAddrTranslator) (const char* addr, size_t offs, size_t size);
    void memDisplayInstallAddrTranslator(memDisplayAddrTranslator handler);

This is an alternative function to install an address translation.
It can be used instead of `memDisplayInstallAddrHandler` if there is no
fixed set of address space names. Instead the translator function
parses the `addr` string each time it is called. The content of `offset`
should be added to the value in the string before conversion to a pointer.

## Utility functions

For the convenience of other software, some utility functions are exported.

    unsigned long long strToSize(const char* str, char** endptr);
    char* sizeToStr(unsigned long long size, char* str);
    volatile void* strToPtr(const char* addrstr, size_t size);

`strToSize` converts a string containing integer numbers (decimal or
hex with `0x` prefix)  and unit prefixes like `k`, `M`, `G`, `T`, `P`, `E`
to an integer number. The unit prefixes assume powers of 1024,
as usual for memory sizes. Valid examples are:
  * 512
  * 0x1000
  * 1k
  * 1G3M5k4

`sizeToStr` converts an integer number to a sting of the form `0xXXXXX=aGbMckd`,
i.e. a hexadecimal number, a `=` and a string with unit prefixes as above.
The numeric components of the string will be in the range 1-1023 with all
components with the value 0 skipped (except for the value 0 itself).

`stringToPtr` converts a string of the form `addrspace:offset` to a pointer
using the installed address space handlers to look up an address mapping.
Again, unit prefixes are allowed, e.g. `A32:1G`.
