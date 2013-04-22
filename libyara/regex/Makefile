CC = gcc
CFLAGS = -O2
OBJS = regex-pcre.o

.SUFFIXES : .o .c

.c.o :
	${CC} ${CFLAGS} -c $<

libregex.a: ${OBJS}
	ar rv libregex.a $?
