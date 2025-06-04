CC ?= cc
CFLAGS ?= -Wall -Wextra -std=c99

SRCS := src/builtins.c src/execute.c src/history.c src/jobs.c src/lineedit.c \
       src/parser.c src/main.c

vush: $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

clean:
	rm -f vush *.o


test: vush
	cd tests && ./run_tests.sh
