INCS = -I../libgit2/include
LIBS = -L../libgit2/build -lgit2 -lpthread -lz -lc

SCAN_DIR = /git

CPPFLAGS = -D_BSD_SOURCE -DSCAN_DIR=\"${SCAN_DIR}\"
CFLAGS = -Os -std=c99 -Wall -Wextra -pedantic ${CPPFLAGS} ${INCS}
LDFLAGS = -s -static ${LIBS}

CC = cc
