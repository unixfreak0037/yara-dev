all:
	flex -d lex.l
	bison -y --defines=grammar.h grammar.y
	gcc -O2 -c *.c
	cd regex && make all
