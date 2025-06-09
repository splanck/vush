# vush

`vush` is a simple UNIX shell written in C. It supports basic command execution
and a few built-in commands.

## Features

- Command line parsing with rudimentary quoting support
- Execution of external commands via `fork` and `exec`
 - Built-in commands: `cd`, `pushd`, `popd`, `dirs`, `exit`, `pwd`, `jobs`, `fg`,
  `bg`, `kill`, `export`, `unset`, `history`, `alias`, `unalias`, `return`, `shift`, `let`, `set`,
  `type`, `source` (or `.`), and `help`
- Environment variable expansion using `$VAR` or `${VAR}` syntax
- `$?` expands to the exit status of the last foreground command
- Wildcard expansion for unquoted `*` and `?` patterns
- Command substitution using backticks or `$(...)`
- Arithmetic expansion using `$((...))` and a `let` builtin
- Background job management using `&`
- Simple pipelines using `|` to connect commands
- Command chaining with `;`, `&&`, and `||`
- Shell functions using `name() { ... }` syntax and a `return` builtin
- `case` selection statements with optional fall-through using `;&`
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
- Shell options toggled with `set -e`, `set -u` and `set -x`

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
interactively or pass `-c` followed by a command string.

```
./vush                # interactive mode
./vush scriptfile     # run commands from "scriptfile"
./vush -c "echo hi"   # run a single command and exit
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
vush> export CDPATH=/usr
vush> cd bin
/usr/bin
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

### Line Continuations

When a line ends with an unescaped backslash the next line is joined before
parsing. This works in scripts and startup files.

```
$ cat script.vsh
echo one \
two
$ ./vush script.vsh
one two
```

### Positional Parameters

When a script file is executed, the remaining command line arguments are stored
as positional parameters.  `$0` expands to the script path while `$1` through
`$9` contain subsequent arguments.  `$@` expands to all parameters separated by
spaces and `$#` gives the count of arguments.  The `shift` builtin discards the
first parameter and shifts the rest down.

## Assignments

Words of the form `NAME=value` placed at the beginning of a command only affect
that command's environment:

```sh
vush> FOO=bar echo $FOO
bar
vush> echo $FOO

```

Without a following command the assignment creates a shell variable that is not
exported but persists for later use:

```sh
vush> BAR=baz
vush> echo $BAR
baz
```

## Built-in Commands

- `cd [dir]` - change the current directory. Without an argument it switches to `$HOME`. `~user` names are expanded using the password database. After a successful change `PWD` and `OLDPWD` are updated. Use `cd -` to print and switch to `$OLDPWD`. If `dir` does not begin with `/` or `.`, each directory listed in the `CDPATH` environment variable is searched. When a `CDPATH` entry is used the resulting path is printed.
- `pushd dir` - push the current directory and change to `dir`.
- `popd` - return to the directory from the stack.
- `dirs` - display the directory stack.
- `exit [status]` - terminate the shell with an optional status code.
- `pwd` - print the current working directory.
- `jobs` - list background jobs started with `&`.
- `fg ID` - wait for background job `ID`.
- `bg ID` - resume a stopped background job `ID`.
- `kill [-SIGNAL] ID` - send a signal to the background job `ID`.
- `export NAME=value` - set an environment variable for the shell.
- `unset NAME` - remove an environment variable.
- `history [-c|-d NUMBER]` - show command history, clear it with `-c`, or delete a specific entry with `-d`.
  Entries are read from and written to the file specified by `VUSH_HISTFILE`
  (default `~/.vush_history`). History size is controlled by the
  `VUSH_HISTSIZE` environment variable (default 1000).
- `alias NAME=value` - define an alias or list all aliases when used without arguments.
- `unalias NAME` - remove an alias.
- `shift` - drop the first positional parameter.
- Aliases are stored in the file specified by `VUSH_ALIASFILE` (default
  `~/.vush_aliases`).
  The file contains one `name=value` pair per line without quotes.
- `type NAME...` - display how each NAME would be interpreted.
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

Here-documents feed inline text to a command:

```
vush> cat <<EOF
hello
EOF
hello
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

## Conditionals and Loops

```
vush> if test 1 -eq 1; then echo yes; else echo no; fi
yes
vush> for x in a b c; do echo $x; done
a
b
c
vush> export i=0; while test $i -lt 2; do echo $i; export i=$(expr $i + 1); done
0
1
vush> case 1 in 1) echo one ;& 2) echo two ;; esac
one
two
```

## Function Example

```
vush> greet() { echo Hello $1; }
vush> greet world
Hello world
vush> greet foo; echo $?
Hello foo
0
```

## Read Example

Create a small script that reads a line and prints it back:

```sh
echo "Enter something:";
read line;
echo "You typed: $line";
```

Running it with redirected input will feed the line to `read`:

```sh
$ printf "hello\n" | ./vush script.vsh
You typed: hello
```

## Type Example

```
vush> alias ll='ls -l'
vush> type ll cd ls unknown
ll is an alias for 'ls -l'
cd is a builtin
ls is /bin/ls
unknown not found
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
