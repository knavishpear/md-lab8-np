CC = gcc
CFLAGS = -Wall

all: drone8

drone8: drone8.c
	$(CC) $(CFLAGS) drone8.c -o drone8 -lm

clean:
	rm -f drone8
