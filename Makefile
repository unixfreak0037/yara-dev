CC = gcc
CFLAGS = -O2 -I libyara
OBJS = yara.o
LIBS = -lfl -lpcre -pthread

.SUFFIXES : .o .c

.c.o :
	${CC} ${CFLAGS} -c $<

yara: ${OBJS} config.h REVISION libyara/libyara.a libyara/regex/libregex.a
	${CC} ${CFLAGS} -O2 ${OBJS} libyara/libyara.a libyara/regex/libregex.a ${LIBS} -o $@

libyara/libyara.a:
	cd libyara && make

libyara/regex/libregex.a:
	cd libyara/regex && make

clean:
	rm -f yara *.o libyara/*.o libyara/regex/*.o libyara/lex.yy.c libyara/y.tab.c libyara/grammar.c libyara/libyara.a libyara/regex/libregex.a libyara/grammar.h
