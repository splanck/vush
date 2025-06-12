# vush

`vush` is a simple UNIX shell written in C. It supports basic command execution
and a few built-in commands. The newly revamped
[manual page](docs/vush.1) provides a concise reference to all options and
usage.

Current version: 0.1.0

## Features

- Command line parsing with rudimentary quoting support
- Execution of external commands via `fork` and `exec`
- Extensive built-in commands handle job control, variable management and file
  operations (see "Built-in Commands" below).

- Environment variable expansion using `$VAR`, `${VAR}` and forms like
  `${VAR:-word}`, `${VAR:=word}`, `${VAR:+word}`, `${VAR#pat}`, `${VAR##pat}`,
- `${VAR%pat}`, `${VAR%%pat}` and `${#VAR}`
- `$?` expands to the exit status of the last foreground command
- `$$` expands to the PID of the running shell and `$!` to the last background job
- `$PPID` expands to the PID of the shell's parent process
- `$-` expands to the currently enabled option letters
- `$LINENO` expands to the current input line number
 - Wildcard expansion for unquoted `*` and `?` patterns (disable with `set -f`,
   re-enable with `set +f`)
- Brace expansion for patterns like `{foo,bar}` and `{1..3}`
- Command substitution using backticks or `$(...)`
- Arithmetic expansion using `$((...))` and a `let` builtin
- Numbers may specify a base using `<base>#<digits>` inside arithmetic expressions
- Background job management using `&`
- Simple pipelines using `|` to connect commands
- Process substitution using `<(cmd)` and `>(cmd)`
- Command chaining with `;`, `&&`, and `||`
- Command negation using `! command`
- Subshells using `( ... )` to group commands
- Brace groups using `{ ... ; }` executed in the current shell
- Shell functions defined with `name() { ... }` or `function name { ... }` and a `return` builtin
- Conditional expressions using `[[ ... ]]` with pattern matching
- POSIX `test` builtin supporting string, numeric and file operators
  like `-e`, `-f`, `-d`, `-r`, `-w`, `-x`, `-b`, `-c`, `-p`, `-h`/`-L`, `-s`,
  `-O`, `-G`, `-u`, `-g`, `-k`, `-S` and `-t`. The unary `!` operator and binary `-a`/`-o` are recognized with standard precedence. Binary comparisons `file1 -nt file2`, `file1 -ot file2` and `file1 -ef file2` are also available
- `case` selection statements with optional fall-through using `;&`
- `select` loops presenting a numbered menu of choices
 - Input and output redirection with `<`, `>`, `>|`, `>>`, `2>`, `2>>` and `&>`,
   including descriptor duplication like `2>&1` or `>&file`, descriptor
   closure using `>&-` or `2>&-`, and numbered descriptors such as `3>` or `4<`
- Persistent command history saved to `~/.vush_history` (overridable with `VUSH_HISTFILE`)
- Maximum history size of 1000 entries (overridable with `VUSH_HISTSIZE`)
- Alias definitions persisted in `~/.vush_aliases` (overridable with `VUSH_ALIASFILE`)
- Function definitions persisted in `~/.vush_funcs` (overridable with `VUSH_FUNCFILE`)
- Variables can be removed with `unset -v` and functions with `unset -f`
- Arrow-key command line editing with history recall
- `Ctrl-A`/`Home` moves to the beginning of the line, `Ctrl-E`/`End` to the end
  and `Ctrl-U` clears back to the start
- Startup commands read from `~/.vushrc` if the file exists
- Additional startup commands read from the file named by the `ENV`
  environment variable if set
- Prompt string configurable via the `PS1` environment variable (see [docs/vush.1](docs/vush.1) for details)
- `exit` accepts an optional status argument
 - Shell options toggled with `set -e`, `set -u`, `set -x`, `set -v`, `set -n`,
  `set -f`/`set +f`, `set -C`/`set +C`, `set -a`, `set -b`/`set +b`, `set -m`/`set +m` and `set -o OPTION` such as
  `pipefail` or `noclobber`
- Use `>| file` to force overwriting a file when `noclobber` is active
- `set --` can replace positional parameters inside the running shell
- Array assignments and `${name[index]}` access
- Here-documents (`<<`) and here-strings (`<<<`)
- History expansion with `!!`, `!n`, `!prefix`, `!$`, `!*`

### Built-in Commands

The shell includes many built-ins. The brief summaries below show what each one
does; consult [docs/vushdoc.md](docs/vushdoc.md) for complete usage details.

#### Job Control
- `bg [ID]` &ndash; resume a stopped job in the background (uses the last
  job when ID is omitted)
- `fg [ID]` &ndash; bring a background job to the foreground (defaults to
  the most recent job)
- `jobs [-l|-p] [ID]` &ndash; list active jobs
- `kill [-s SIG|-l] ID|PID` &ndash; send a signal. `kill -l NUM` prints the
  signal name for `NUM`.
- `wait [ID|PID]` &ndash; wait for a job or process to finish
- `trap [-p|-l|'cmd' SIGNAL]` &ndash; run a command when a signal is received
- `set -b` &ndash; notify when background jobs complete
- `set -m` &ndash; enable job control features

#### Variable Management
- `export [-p|-n NAME] NAME[=VALUE]` &ndash; set or display exported variables
- `readonly [-p] NAME[=VALUE]` &ndash; mark variables read-only
- `local NAME[=VALUE]` &ndash; define a local variable in a function
- `unset [-f|-v] NAME` &ndash; remove functions with `-f`, variables with `-v`, or both
- `set [options] [-- args...]` &ndash; change shell options or parameters
- `shift [N]` &ndash; rotate positional parameters
- `alias [-p] [NAME[=VALUE]]` &ndash; define command aliases
- `unalias [-a] NAME` &ndash; remove command aliases
- `let EXPR` &ndash; evaluate arithmetic expressions
- `getopts OPTSTRING VAR` &ndash; parse positional parameters

#### File and Resource Operations
- `cd [-L|-P] [dir]` &ndash; change directories
- `pushd dir`, `popd`, `dirs` &ndash; manage the directory stack
- `pwd [-L|-P]` &ndash; print the current directory
- `umask [-S] [mask]` &ndash; set or display the file creation mask
- `ulimit [-HS] [-a|-c|-d|-f|-n|-s|-t|-v [limit]]` &ndash; view or set resource limits

#### Command Execution and Utilities
- `command [-p|-v|-V] NAME [args...]` &ndash; run a command without function lookup
- `eval WORDS...` &ndash; execute constructed commands
- `exec command [args...]` &ndash; replace the shell with a command
- `source file [args...]` (or `.`) &ndash; read commands from a file
- `time command` and `times` &ndash; report timing statistics
- `hash [-r] [name...]` &ndash; manage the command path cache
- `type NAME...` &ndash; show how a command would be interpreted
- `fc` and `history` &ndash; edit or list command history
- `echo [-n] [-e] [args...]` &ndash; print text
- `printf FORMAT [args...]` &ndash; formatted output
- `read [-r] [-a NAME] [VAR...]` &ndash; read a line of input, storing it in
  `$REPLY` when no variables are listed
- `test EXPR` and `[[ EXPR ]]` &ndash; evaluate conditions
- `:`/`true`/`false` &ndash; return fixed status codes
- `return [status]` &ndash; exit from a function
- `break [N]` and `continue [N]` &ndash; loop control
- `help` &ndash; display summaries of built-ins
- `exit [status]` &ndash; terminate the shell
- Extended parameter expansion for pattern replacement `${var/pat/repl}`, substrings `${var:off[:len]}` and error checking `${var?word}`

## Building

Use the provided `Makefile` to build the shell:

```sh
make
```

The resulting binary will be `./vush`. Remove the binary with:

```sh
make clean
```

## Installation

Install the binary and manual page with:

```sh
make install
```

By default files are placed under `/usr/local`. Set `PREFIX` to a different
location if desired. Uninstall with:

```sh
make uninstall
```

## Configuration

Startup behavior and saved state can be customized with a few files and
environment variables:

- `~/.vushrc` runs before the first prompt if present.
- `ENV` can specify an additional startup file executed after `~/.vushrc`.
- `VUSH_HISTFILE` controls where history is saved (default `~/.vush_history`).
- `VUSH_HISTSIZE` limits how many entries are kept in the history file
  (default 1000).
- `VUSH_ALIASFILE` holds persistent aliases (default `~/.vush_aliases`).
- `VUSH_FUNCFILE` holds persistent functions (default `~/.vush_funcs`).
- `PS1` sets the command prompt displayed before each input line.
- `PS2` is shown when more input is needed, such as for unmatched quotes.
- `PS3` is the prompt used by the `select` builtin.
- `PS4` prefixes trace output produced by `set -x`.
- `MAIL` names a mailbox file checked before each prompt. A notice is printed
  when it is modified.
- `MAILPATH` may list multiple mailbox files separated by `:`. Each triggers a
  `New mail in <file>` message when updated.
- `CDPATH` provides directories searched by `cd` for relative paths. `cd` also
  accepts `-L` (logical, default) and `-P` (physical) to control how paths are
  resolved. With `-L` `PWD` reflects the logical path while `-P` resolves the
  target with `realpath()` and sets `PWD` to the physical location.
- `pwd` prints the current directory. Use `-P` to display the physical directory from `getcwd()` while `-L` (the default) uses the value of `$PWD`.

Examples:

```sh
# Save only 200 entries to a custom history file
export VUSH_HISTFILE=~/my_history
export VUSH_HISTSIZE=200

# Change the prompt
export PS1='mysh> '

# Search projects and /tmp when changing directories
export CDPATH=~/projects:/tmp
cd repo
# Follow symlinks using -P
cd -P /tmp/my_link

# Mark a variable for export without changing its value
MYVAR=example
export MYVAR
# Create an empty read-only variable
readonly MYCONST
```

```sh
# Simple file tests
test -d /tmp && echo "tmp exists"
test -h /bin/sh && echo "linked shell"
test file1 -nt file2 && echo "file1 newer"
test file1 -ef link && echo "same file"
```

```sh
# Read with no variable names stores the line in $REPLY
read
echo "You typed $REPLY"
```

```sh
# Limit core dumps and inspect limits
ulimit -c 0
ulimit -a
# Set a hard file descriptor limit and view the soft stack limit
ulimit -H -n 4096
ulimit -S -s
```

```sh
# Arithmetic using different bases
echo $((16#ff + 2#10))
```

## Usage

Run `./vush` for an interactive shell or pass a script file or `-c` string.
Run `vush --version` (or `vush -V`) to print the version.
See [docs/vushdoc.md](docs/vushdoc.md) for full usage details and examples.

## Documentation

See [docs/vush.1](docs/vush.1) for the manual page.

## Tests

Ensure `expect` is installed and run:

```sh
make test
```

The test scripts under `tests/` will launch `./vush` with predefined commands and verify the output.

## Debugging

Compile with debugging information so that stack traces are produced:

```sh
make clean
make CFLAGS="-g -Wall -Wextra -std=c99"
```

Run the shell under `gdb` or `valgrind` to catch crashes. Using

```sh
valgrind ./vush
```

will record a backtrace on failure, making it easier to locate memory errors.

## License

This project is licensed under the terms of the GNU General Public License
version 3. See [LICENSE](LICENSE) for the full license text.
