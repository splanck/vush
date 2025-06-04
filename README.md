# vush

`vush` is a simple UNIX shell written in C. It supports basic command execution
and a few built-in commands.

## Features

- Command line parsing with rudimentary quoting support
- Execution of external commands via `fork` and `exec`
- Built-in commands: `cd`, `exit`, `pwd`, `jobs`, `fg`, `kill`, `source`, and `help`
- Environment variable expansion for tokens beginning with `$`
- `$?` expands to the exit status of the last foreground command
- Wildcard expansion for unquoted `*` and `?` patterns
- Background job management using `&`
- Simple pipelines using `|` to connect commands
- Command chaining with `;`, `&&`, and `||`
- Input and output redirection with `<`, `>` and `>>`
- Persistent command history saved to `~/.vush_history`
- Prompt string configurable via the `PS1` environment variable

## Building

Use the provided `Makefile` to build the shell:

```sh
make
```

The resulting binary will be `./vush`. Remove the binary with:

```sh
make clean
```

## Usage

Run the `vush` binary and enter commands as you would in a normal shell.  You
can also provide a filename to execute commands from a script non-
interactively.

```
./vush                # interactive mode
./vush scriptfile     # run commands from "scriptfile"
```

Example interactive session:

```
vush> ls -l
vush> cd /tmp
vush> cd -
/home/user
vush> echo $HOME
vush> sleep 5 &
```

## Quoting and Expansion

Words beginning with `$` expand to environment variables. A leading `~` is
replaced with the value of `$HOME`.

Single quotes disable all expansion. Double quotes preserve spaces while still
expanding variables. Use a backslash to escape the next character. A `#` that
appears outside of quotes starts a comment and everything after it on the line
is ignored.

Unquoted words containing `*` or `?` are expanded to matching filenames.  If no
files match, the pattern is left unchanged.

```
vush> echo '$HOME is not expanded'
$HOME is not expanded
vush> echo "$HOME"
/home/user
vush> echo \$HOME
$HOME
vush> false
vush> echo $?
1
vush> true
vush> echo $?
0
```

## Built-in Commands

- `cd [dir]` - change the current directory. Without an argument it switches to `$HOME`. After a successful change `PWD` and `OLDPWD` are updated. Use `cd -` to print and switch to `$OLDPWD`.
- `exit` - terminate the shell.
- `pwd` - print the current working directory.
- `jobs` - list background jobs started with `&`.
- `fg ID` - wait for background job `ID`.
- `kill [-SIGNAL] ID` - send a signal to the background job `ID`.
- `export NAME=value` - set an environment variable for the shell.
- `history` - show previously entered commands.
  Entries are read from and written to `~/.vush_history`.
- `source file` or `. file` - execute commands from a file.
- `help` - display information about built-in commands.

## Redirection Examples

```
vush> echo hello >out.txt
vush> cat < out.txt
hello
vush> echo again >>out.txt
```

## Background Jobs Example

```
vush> sleep 3 &
vush> jobs
[1] 1234 sleep 3
vush> # continue using the shell
[vush] job 1234 finished
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
