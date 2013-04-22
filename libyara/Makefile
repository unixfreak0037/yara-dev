CC = gcc
CFLAGS = -O2
OBJS = ast.o proc.o weight.o xtoi.o eval.o exe.o filemap.o hash.o libyara.o mem.o scan.o lex.yy.o y.tab.o
HEADERS = ast.h elf.h eval.h exe.h filemap.h grammar.h hash.h lex.h mem.h pe.h proc.h regex.h scan.h sizedstr.h weight.h xtoi.h yara.h

.SUFFIXES : .o .c

.c.o :
	${CC} ${CFLAGS} -c $<

libyara.a: ${OBJS} ${HEADERS}
	cd regex && make
	ar rv libyara.a $?

lex.yy.c: lex.l grammar.h
	flex -d $?

grammar.h: grammar.y
	bison -y --defines=grammar.h $?

y.tab.o: y.tab.c
	${CC} ${CFLAGS} -c $?
