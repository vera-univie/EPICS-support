TOP=.
include $(TOP)/configure/CONFIG

memDisplayDev_DBD += base.dbd
LIB_LIBS += $(EPICS_BASE_IOC_LIBS)

# library
LIBRARY = memDisplay

INC += memDisplay.h
DBDS = memDisplay.dbd

LIB_SRCS += memDisplay.c
LIB_SRCS += memDisplay_shell.c

include $(TOP)/configure/RULES
