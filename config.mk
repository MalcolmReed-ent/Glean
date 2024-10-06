# glean version
VERSION = 1.0

# Customize below to fit your system

# paths
PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

# includes and libs
INCS = -I. -I/usr/include -I/usr/include/json-c
LIBS = -L/usr/lib -lcurl -ljson-c

# flags
CPPFLAGS = -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_POSIX_C_SOURCE=200809L -DVERSION=\"${VERSION}\"
CFLAGS   = -std=c99 -pedantic -Wall -Wextra -Wno-sign-compare -Wno-unused-parameter -O3 -ffast-math -march=native ${INCS} ${CPPFLAGS}
LDFLAGS  = -s ${LIBS}

# Debugging flags (uncomment for debugging)
#CFLAGS += -g -DDEBUG

# OpenMP support (uncomment to enable parallel processing)
#CFLAGS += -fopenmp
#LDFLAGS += -fopenmp

# compiler and linker
CC = cc

# Use clang if available (usually produces faster code)
ifneq ($(shell which clang),)
    CC = clang
endif
