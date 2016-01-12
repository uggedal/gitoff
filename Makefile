include config.mk

SRC = gitoff.c compat/reallocarray.c compat/strlcpy.c
OBJ = ${SRC:.c=.o}

all: gitoff

.c.o:
	${CC} -c ${CFLAGS} -o $@ -c $<

${OBJ}: config.mk

gitoff: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	rm -f gitoff ${OBJ}

.PHONY: clean
