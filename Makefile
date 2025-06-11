CC ?= cc
CFLAGS ?= -Wall -Wextra -std=c99
PREFIX ?= /usr/local
MANPREFIX ?= $(PREFIX)/share/man

SRCS := src/builtins.c src/builtins_fs.c src/builtins_jobs.c \
       src/builtins_alias.c src/builtins_func.c src/builtins_vars.c \
       src/builtins_read.c src/builtins_getopts.c src/vars.c \
       src/builtins_misc.c src/builtins_history.c src/builtins_time.c \
       src/builtins_signals.c src/execute.c src/history.c \
       src/jobs.c src/lineedit.c src/history_search.c src/completion.c \
       src/parser.c src/lexer.c src/lexer_token.c src/lexer_expand.c src/arith.c \ src/parser_utils.c src/parser_clauses.c \
       src/parser_pipeline.c src/lexer.c src/arith.c \
       src/dirstack.c src/util.c src/pipeline.c src/func_exec.c \
       src/hash.c \
       src/main.c

vush: $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

clean:
	rm -f vush *.o


test: vush
	cd tests && ./run_tests.sh

install: vush
	install -d $(PREFIX)/bin
	install -m 755 vush $(PREFIX)/bin
	install -d $(MANPREFIX)/man1
	install -m 644 docs/vush.1 $(MANPREFIX)/man1

uninstall:
	rm -f $(PREFIX)/bin/vush
	rm -f $(MANPREFIX)/man1/vush.1
