ifeq ($(wildcard /ioc/tools/driver.makefile),)
$(warning It seems you do not have the PSI build environment. Remove GNUmakefile.)
include Makefile
else
include /ioc/tools/driver.makefile

BUILDCLASSES += vxWorks Linux WIN32
SOURCES = memDisplay.c memDisplay_shell.c

HEADERS = memDisplay.h

endif
