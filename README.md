# vush

`vush` is a lightweight UNIX shell written in C. It provides advanced job
control, history management, rich expansions and a configurable environment
while staying slim. The [vush.1](docs/vush.1) page contains the complete
manual, and [docs/vushdoc.md](docs/vushdoc.md) holds a more detailed tutorial.

Current version: 0.1.0

**DISCLAIMER**:
vush is currently in development and should not be considered stable software. This project is under active construction and likely contains bugs, incomplete features, and unstable behavior. Use at your own risk. We do not recommend relying on vush for critical or production use at this time.

## Features

- Execute external commands and built-ins with job control
- Command history with line editing
- Alias and function definitions saved across sessions
- Shell scripting constructs: loops, conditionals, and subshells
- Expansions for variables, globbing, command substitution, and arithmetic
- I/O redirection and pipelines
- Configurable environment via startup files and shell options

See [docs/vushdoc.md](docs/vushdoc.md) for complete details.

### Built-in Commands

vush includes a wide range of built-ins for job control, variable management, file operations, and more. See [docs/vushdoc.md](docs/vushdoc.md) for the full reference.

## Roadmap

The project is currently at **version 0.1.0** and will progress through the following milestones:

* **0.1.x – Initial Development Phase** (current phase)
* **0.2.x – Feature Incomplete Unstable Public Releases**
* **0.3.x – Near Feature Complete Unstable Versions**
* **0.4.x – Feature Complete Test Builds**
* **0.9.x – Feature Complete Release Candidates**
* **1.0 – Initial Version Final Release**

## Building

Use the provided `Makefile` to build the shell:

```sh
make
```

The resulting binary will be `build/vush`. Remove the directory with:

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
- Use `set -p` to skip these startup files inside scripts that require a
  predictable environment.
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
  `New mail in <file>` message when updated. Memory used to remember mailbox
  timestamps is released when the shell exits.
- `CDPATH` provides directories searched by `cd` for relative paths. `cd` also
  accepts `-L` (logical, default) and `-P` (physical) to control how paths are
  resolved. With `-L` `PWD` reflects the logical path while `-P` resolves the
  target with `realpath()` and sets `PWD` to the physical location.
- `SHELL` contains the path used to invoke `vush`.
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

```sh
# Measure execution time with POSIX formatting
time -p sleep 0.1
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

You can also run a specific group of tests by invoking one of the helper
scripts under `tests/`:

```sh
cd tests
./run_alias_tests.sh       # alias related tests
./run_var_tests.sh         # variable handling tests
./run_builtins_tests.sh    # built-in command tests
./run_history_tests.sh     # history and ! expansion tests
```

The test scripts under `tests/` will launch `build/vush` with predefined commands and verify the output.

## Debugging

Compile with debugging information so that stack traces are produced:

```sh
make clean
make CFLAGS="-g -Wall -Wextra -std=c99"
```

Run the shell under `gdb` or `valgrind` to catch crashes. Using

```sh
gdb ./vush
```

will record a backtrace on failure, making it easier to locate memory errors.

## License

This project is licensed under the terms of the GNU General Public License
version 3. See [LICENSE](LICENSE) for the full license text.
