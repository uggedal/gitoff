include config.mk

HDR = style.h util.h compat.h
SRC = gitoff.c util.c compat/reallocarray.c compat/strlcpy.c
OBJ = ${SRC:.c=.o}

all: gitoff

.c.o:
	${CC} -c ${CFLAGS} -o $@ -c $<

${OBJ}: config.mk ${HDR}

gitoff: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

style.h: style.css
	printf 'const char *STYLE = "' > style.h
	sed 's/"/\\"/;s/$$/\\n\\/' style.css >> style.h
	printf '";\n' >> style.h

clean:
	rm -f gitoff ${OBJ}

.PHONY: clean
