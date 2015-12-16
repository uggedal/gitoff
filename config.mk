INCS = -I/usr/local/include
LIBS = -L/usr/local/lib -lgit2 -lc

SCAN_DIR = /var/git
MAX_REPOS = 100

CPPFLAGS = -D_BSD_SOURCE \
		   -DSCAN_DIR=\"${SCAN_DIR}\" \
		   -DMAX_REPOS=${MAX_REPOS}
CFLAGS = -Os -std=c99 -Wall -Wextra -pedantic ${CPPFLAGS} ${INCS}
LDFLAGS = ${LIBS}

CC = cc
