yara:
	cd libyara && make
	gcc -O2 -I libyara -c yara.c
	gcc -O2 *.o libyara/*.o libyara/regex/*.o -lfl -lpcre -pthread -o yara

clean:
	rm -f yara *.o libyara/*.o libyara/regex/*.o libyara/lex.yy.c libyara/y.tab.c
