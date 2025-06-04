CC ?= cc
CFLAGS ?= -Wall -Wextra -std=c99

vush: src/main.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f vush *.o
