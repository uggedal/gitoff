INCS = -I/usr/local/include
LIBS = -L/usr/local/lib -lgit2 -lc

CPPFLAGS = -D_BSD_SOURCE
CFLAGS = -Os -std=c99 -Wall -Wextra -pedantic ${CPPFLAGS} ${INCS}
LDFLAGS = ${LIBS}

CC = cc
