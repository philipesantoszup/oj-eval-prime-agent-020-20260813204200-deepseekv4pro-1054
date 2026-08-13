.PHONY: all clean
all:
	gcc -o code main.c buddy.c -O2 -Wall
clean:
	rm -f code test *.o
