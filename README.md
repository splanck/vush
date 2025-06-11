# vush

`vush` is a simple UNIX shell written in C. It supports basic command execution
and a few built-in commands.

Current version: 0.1.0

## Features

- Command line parsing with rudimentary quoting support
- Execution of external commands via `fork` and `exec`
 - Built-in commands: `alias`, `bg`, `break`, `cd`, `command`, `continue`,
 `dirs`, `echo`, `eval`, `exec`, `exit`, `export`, `false`, `fc`, `fg`, `getopts`, `hash`,
  `help`, `history`, `jobs`, `kill`, `let`, `local`, `popd`, `printf`, `pushd`,
  `pwd`, `read`, `readonly`, `return`, `set`, `shift`, `source` (or `.`), `test`,
  `time`, `times`, `trap`, `true`, `type`, `ulimit`, `umask`, `unalias`, `unset`, `wait`, and `:`
- Environment variable expansion using `$VAR`, `${VAR}` and forms like
  `${VAR:-word}`, `${VAR:=word}`, `${VAR:+word}`, `${VAR#pat}`, `${VAR##pat}`,
  `${VAR%pat}`, `${VAR%%pat}` and `${#VAR}`
- `$?` expands to the exit status of the last foreground command
- Wildcard expansion for unquoted `*` and `?` patterns (disable with `set -f`)
- Brace expansion for patterns like `{foo,bar}` and `{1..3}`
- Command substitution using backticks or `$(...)`
- Arithmetic expansion using `$((...))` and a `let` builtin
- Background job management using `&`
- Simple pipelines using `|` to connect commands
- Process substitution using `<(cmd)` and `>(cmd)`
- Command chaining with `;`, `&&`, and `||`
- Subshells using `( ... )` to group commands
- Brace groups using `{ ... ; }` executed in the current shell
- Shell functions using `name() { ... }` syntax and a `return` builtin
- Conditional expressions using `[[ ... ]]` with pattern matching
- `case` selection statements with optional fall-through using `;&`
- `select` loops presenting a numbered menu of choices
- Input and output redirection with `<`, `>`, `>>`, `2>`, `2>>` and `&>`,
  including descriptor duplication like `2>&1` or `>&file`
- Persistent command history saved to `~/.vush_history` (overridable with `VUSH_HISTFILE`)
- Maximum history size of 1000 entries (overridable with `VUSH_HISTSIZE`)
- Alias definitions persisted in `~/.vush_aliases` (overridable with `VUSH_ALIASFILE`)
- Function definitions persisted in `~/.vush_funcs` (overridable with `VUSH_FUNCFILE`)
- Arrow-key command line editing with history recall
- `Ctrl-A`/`Home` moves to the beginning of the line, `Ctrl-E`/`End` to the end
  and `Ctrl-U` clears back to the start
- Startup commands read from `~/.vushrc` if the file exists
- Prompt string configurable via the `PS1` environment variable (see [docs/vush.1](docs/vush.1) for details)
- `exit` accepts an optional status argument
- Shell options toggled with `set -e`, `set -u`, `set -x`, `set -n`, `set -f` and `set -o OPTION` such as `pipefail` or `noclobber`
- `set --` can replace positional parameters inside the running shell
- Array assignments and `${name[index]}` access
- Here-documents (`<<`) and here-strings (`<<<`)
- History expansion with `!!`, `!n`, `!prefix`, `!$`, `!*`
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
- `VUSH_HISTFILE` controls where history is saved (default `~/.vush_history`).
- `VUSH_HISTSIZE` limits how many entries are kept in the history file
  (default 1000).
- `VUSH_ALIASFILE` holds persistent aliases (default `~/.vush_aliases`).
- `VUSH_FUNCFILE` holds persistent functions (default `~/.vush_funcs`).
- `PS1` sets the command prompt displayed before each input line.
- `PS2` is shown when more input is needed, such as for unmatched quotes.
- `PS3` is the prompt used by the `select` builtin.
- `PS4` prefixes trace output produced by `set -x`.
- `CDPATH` provides directories searched by `cd` for relative paths.

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
