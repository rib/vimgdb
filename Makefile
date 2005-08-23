

all:
	gcc -o vimgdb vimgdb.c -lutil -g

clean:
	rm *.o
	rm *~
	rm vimgdb