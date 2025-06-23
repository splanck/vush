CC ?= cc
CFLAGS ?= -Wall -Wextra -std=c99

ifndef NOEXECSTACK_FLAG
NOEXECSTACK_FLAG := $(shell printf 'int main(){}' | $(CC) -Wa,--noexecstack -c -x c -o /dev/null - >/dev/null 2>&1 && echo -Wa,--noexecstack)
endif

CFLAGS += $(NOEXECSTACK_FLAG)
PREFIX ?= /usr/local
MANPREFIX ?= $(PREFIX)/share/man
BUILDDIR := build
OBJDIR := $(BUILDDIR)

.PHONY: clean test install uninstall

# Feature checks
HAVE_FEXECVE := $(shell printf '#define _GNU_SOURCE\n#include <unistd.h>\nint main(){fexecve(0,(char*[]){0},(char*[]){0});return 0;}' | $(CC) $(CFLAGS) -x c - -o /dev/null >/dev/null 2>&1 && echo 1 || echo 0)
ifeq ($(HAVE_FEXECVE),1)
CFLAGS += -DHAVE_FEXECVE
endif

SRCS := src/builtins.c src/builtins_core.c src/builtins_fs.c src/builtins_jobs.c \
       src/builtins_alias.c src/builtins_func.c src/builtins_vars.c \
       src/builtins_read.c src/builtins_getopts.c src/builtins_exec.c src/vars.c \
       src/builtins_misc.c src/builtins_test.c src/builtins_print.c src/builtins_history.c src/builtins_time.c src/builtins_sys.c \
       src/builtins_signals.c src/execute.c src/history_list.c src/history_file.c \
       src/jobs.c src/lineedit.c src/history_search.c src/completion.c \
       src/parser.c src/lexer.c src/lexer_token.c src/lexer_expand.c src/history_expand.c src/param_expand.c src/field_split.c src/quote_utils.c src/prompt_expand.c src/brace_expand.c src/arith.c \
       src/cmd_subst.c \
       src/parser_utils.c src/parser_clauses.c \
       src/parser_pipeline.c src/parser_here_doc.c src/alias_expand.c \
       src/parser_brace_expand.c \
       src/dirstack.c src/util.c src/assignment_utils.c src/pipeline.c src/pipeline_exec.c src/control.c src/redir.c src/func_exec.c \
       src/hash.c src/trap.c src/startup.c src/mail.c src/repl.c \
       src/state_paths.c src/main.c src/strarray.c src/signal_utils.c

OBJS := $(patsubst src/%.c,$(OBJDIR)/%.o,$(SRCS))

$(BUILDDIR)/vush: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

$(OBJDIR)/%.o: src/%.c
	mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILDDIR)


test: $(BUILDDIR)/vush
	cd tests && ./run_tests.sh

install: $(BUILDDIR)/vush
	install -d $(PREFIX)/bin
	install -m 755 $(BUILDDIR)/vush $(PREFIX)/bin
	install -d $(MANPREFIX)/man1
	install -m 644 docs/vush.1 $(MANPREFIX)/man1

uninstall:
	rm -f $(PREFIX)/bin/vush
	rm -f $(MANPREFIX)/man1/vush.1
