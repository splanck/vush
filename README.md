# vush

`vush` is a simple UNIX shell written in C. It supports basic command execution
and a few built-in commands.

## Features

- Command line parsing with rudimentary quoting support
- Execution of external commands via `fork` and `exec`
- Built-in commands: `cd`, `pushd`, `popd`, `exit`, `pwd`, `jobs`, `fg`,
  `bg`, `kill`, `export`, `unset`, `history`, `alias`, `unalias`,
  `source` (or `.`), and `help`
- Environment variable expansion using `$VAR` or `${VAR}` syntax
- `$?` expands to the exit status of the last foreground command
- Wildcard expansion for unquoted `*` and `?` patterns
- Command substitution using backticks or `$(...)`
- Background job management using `&`
- Simple pipelines using `|` to connect commands
- Command chaining with `;`, `&&`, and `||`
- Input and output redirection with `<`, `>`, `>>`, `2>`, `2>>` and `&>`,
  including descriptor duplication like `2>&1` or `>&file`
- Persistent command history saved to `~/.vush_history`
- Maximum history size of 1000 entries (overridable with `VUSH_HISTSIZE`)
- Alias definitions persisted in `~/.vush_aliases`
- Arrow-key command line editing with history recall
- `Ctrl-A`/`Home` moves to the beginning of the line, `Ctrl-E`/`End` to the end
  and `Ctrl-U` clears back to the start
- Startup commands read from `~/.vushrc` if the file exists
- Prompt string configurable via the `PS1` environment variable
- `exit` accepts an optional status argument

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

## Usage

Run the `vush` binary and enter commands as you would in a normal shell.  You
can also provide a filename to execute commands from a script non-
interactively.

```
./vush                # interactive mode
./vush scriptfile     # run commands from "scriptfile"
```

When invoked without a script file, commands from `~/.vushrc` are executed
before the first prompt if that file exists.

Example interactive session:

```
vush> ls -l
vush> cd /tmp
vush> cd -
/home/user
vush> echo $HOME
vush> cd ~otheruser
vush> sleep 5 &
```

## Quoting and Expansion

Words beginning with `$` or `${...}` expand to environment variables. A leading `~` expands
to the current user's home directory while `~user` resolves to that user's
home directory using the system password database.

Single quotes disable all expansion. Double quotes preserve spaces while still
expanding variables. Use a backslash to escape the next character. A `#` that
appears outside of quotes starts a comment and everything after it on the line
is ignored.

Unquoted words containing `*` or `?` are expanded to matching filenames.  If no
files match, the pattern is left unchanged.

Commands enclosed in backticks or `$(...)` are executed and their output
substituted into the word before other expansion occurs.

```
vush> echo '$HOME is not expanded'
$HOME is not expanded
vush> echo "$HOME"
/home/user
vush> echo "${HOME}"
/home/user
vush> echo ${HOME}
/home/user
vush> echo \$HOME
$HOME
vush> echo '${HOME}'
${HOME}
vush> false
vush> echo $?
1
vush> true
vush> echo $?
0
vush> echo $(echo hi)
hi
```

If the closing quote or `)` for command substitution is missing, `vush` prints
`syntax error: unmatched '<char>'` to stderr, sets `$?` to `1` and ignores the
line.

## Built-in Commands

- `cd [dir]` - change the current directory. Without an argument it switches to `$HOME`. `~user` names are expanded using the password database. After a successful change `PWD` and `OLDPWD` are updated. Use `cd -` to print and switch to `$OLDPWD`.
- `pushd dir` - push the current directory and change to `dir`.
- `popd` - return to the directory from the stack.
- `exit [status]` - terminate the shell with an optional status code.
- `pwd` - print the current working directory.
- `jobs` - list background jobs started with `&`.
- `fg ID` - wait for background job `ID`.
- `bg ID` - resume a stopped background job `ID`.
- `kill [-SIGNAL] ID` - send a signal to the background job `ID`.
- `export NAME=value` - set an environment variable for the shell.
- `unset NAME` - remove an environment variable.
- `history [-c]` - show previously entered commands or clear the history.
  Entries are read from and written to `~/.vush_history`.
  History size is controlled by the `VUSH_HISTSIZE` environment variable (default 1000).
- `alias NAME=value` - define an alias or list all aliases when used without arguments.
- `unalias NAME` - remove an alias.
- Aliases are stored in `~/.vush_aliases`.
  The file contains one `name=value` pair per line without quotes.
- `source file` or `. file` - execute commands from a file.
- `help` - display information about built-in commands.

## Redirection Examples

```
vush> echo hello >out.txt
vush> cat < out.txt
hello
vush> echo again >>out.txt
vush> echo error 2>err.txt
vush> ls nonexistent &>both.txt
vush> cat both.txt
vush> echo dup >&dup.txt
vush> ls nonexistent 2>&1 >>dup.txt
```

## Background Jobs Example

```
vush> sleep 3 &
vush> jobs
[1] 1234 sleep 3
vush> # continue using the shell
[vush] job 1234 finished
```

## Directory Stack Example

```
vush> pushd /tmp
/path/to/dir
vush> popd
```

## Documentation

See [docs/vush.1](docs/vush.1) for the manual page and
[docs/CHANGELOG.md](docs/CHANGELOG.md) for the change history.

## Tests

Ensure `expect` is installed and run:

```sh
make test
```

The test scripts under `tests/` will launch `./vush` with predefined commands and verify the output.

## License

This project is licensed under the terms of the GNU General Public License
version 3. See [LICENSE](LICENSE) for the full license text.
