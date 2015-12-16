include config.mk

SRC = gitoff.c
OBJ = ${SRC:.c=.o}

all: gitoff

.c.o:
	${CC} -c ${CFLAGS} $<

${OBJ}: config.mk

gitoff: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	rm -f gitoff ${OBJ}

.PHONY: clean
