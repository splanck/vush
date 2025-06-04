CC ?= cc
CFLAGS ?= -Wall -Wextra -std=c99

SRCS := $(wildcard src/*.c)

vush: $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

clean:
	rm -f vush *.o


test: vush
	cd tests && ./run_tests.sh
