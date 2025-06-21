This guide complements the revamped [vush.1](vush.1) manual page with
detailed explanations and examples.

## Synopsis

`vush [scriptfile]`
`vush -c "command"`
`vush --version`

Invoke without arguments for an interactive shell.

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
before the first prompt if that file exists.  If the `ENV` environment
variable is set, its file is processed afterwards.  The `set -p` option
suppresses both files when enabled.  The `set -t` option exits after one
command is executed.  The `set -h` option caches each command after
execution.

## Options

- `-c command` - execute the specified command string and exit.
- `-V`, `--version` - display version information and exit.

## Startup Files

`~/.vushrc` is executed before the first prompt if it exists. When the `ENV` environment variable is set, the file it names is read after `~/.vushrc`.

### Shebang Scripts

Scripts beginning with `#!/usr/bin/env vush` can be executed directly. Create a
file `hello.vsh` with:

```sh
#!/usr/bin/env vush
echo Hello from vush
```

Make it executable and run it:

```sh
$ chmod +x hello.vsh
$ ./hello.vsh
Hello from vush
```

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
vush> cd -P /tmp/my_link
vush> sleep 5 &
```

## Quoting and Expansion

Words beginning with `$` or `${...}` expand to environment variables. A leading `~` expands
to the current user's home directory while `~user` resolves to that user's
home directory using the system password database.

Single quotes disable all expansion. Double quotes preserve spaces while still
expanding variables. Use a backslash to escape the next character. The form
`$'...'` interprets common backslash escapes like `\n` and `\t` without
performing variable expansion. A `#` that
appears outside of quotes starts a comment and everything after it on the line
is ignored.

Unquoted words containing `*` or `?` are expanded to matching filenames (disable with `set -f`, re-enable with `set +f`).  If no
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
vush> echo $'one\nTwo'
one
Two
vush> false
vush> echo $?
1
vush> true
vush> echo $?
0
vush> echo $(echo hi)
hi
vush> echo $$
12345
vush> echo $PPID
12344
vush> sleep 1 &
vush> echo $!
12346
vush> ! true
vush> echo $?
1
vush> ! false
vush> echo $?
0
```

`$$` expands to the PID of the running shell while `$!` gives the PID of the
most recent background job. `$PPID` yields the parent process ID. `$-` expands
to a string of the current option letters such as `eu` when `set -e` and
`set -u` are enabled. `$LINENO` holds the current input line number.

Additional parameter expansion forms (doubling `#` or `%` removes the
longest matching prefix or suffix).  The `@Q` operator quotes the value:

```
vush> unset TEMP
vush> echo ${TEMP:-default}
default
vush> echo ${TEMP:=setnow}
setnow
vush> echo ${TEMP:+alt}
alt
vush> export TEMP=endings
vush> echo ${TEMP#end}
ings
vush> echo ${TEMP%ings}
end
vush> echo ${#TEMP}
7
vush> FOO='a b'
vush> echo ${FOO@Q}
'a b'
```

Using `?` reports an error when a variable is unset or empty:

```
vush> unset ERR
vush> echo ${ERR:?missing value}
ERR: missing value
vush> echo $?
1
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
as positional parameters.  `$0` expands to the script path and `$1`, `$2`, and
so on expand to subsequent parameters.  Indexes beyond nine can be referenced
using the full number such as `$10` or `$11`.  `$@` expands to all parameters
separated by spaces while `$*` joins them using the first character of `IFS`
(space by default).  `$#` gives the count of arguments.  The `shift` builtin
discards the first *n* parameters (one if omitted) and shifts the rest down.

Splitting honors empty fields when `IFS` contains non-whitespace characters:

```sh
vush> IFS=:
vush> set -- :a::b:
vush> echo "$#"
5
vush> for p in "$@"; do echo "[$p]"; done
[]
[a]
[]
[b]
[]
```

### History Expansion

Previous commands can be reused with `!` expansions. `!!` recalls the last
command. `!n` runs entry *n* from `history` while `!-n` looks *n* commands back.
`!prefix` finds the most recent command starting with *prefix*. `!$` expands to
the last word of the previous command and `!*` expands to all of its words.

```sh
vush> echo one two
vush> !!
echo one two
one two
vush> ls /tmp
vush> echo !$
echo /tmp
/tmp
```

The `fc` builtin can replay or edit previous commands. `fc -l` prints the
specified range and with `-n` the command numbers are omitted. `-r` reverses the
order of the range. Without `-l` the commands are edited using `-e editor`
(defaulting to `$FCEDIT` or `ed`). The `-s` option immediately re-executes the
selected command, optionally replacing the first occurrence of `old` with
`new`.

```sh
vush> fc -l -2
1 echo hi
2 ls
vush> fc -s hi=hello 1
echo hello
hello
```

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
vush> export BAR
```

With `set -k` enabled, assignment words may appear after the command name:

```sh
vush> set -k
vush> sh -c 'echo $FOO' FOO=bar
bar
```

### Arrays

Arrays are defined using `NAME=(word ...)` and expanded with `${NAME[index]}` or
`${NAME[@]}`:

```sh
vush> nums=(one two three)
vush> echo ${nums[1]}
two
vush> for n in ${nums[@]}; do echo $n; done
one
two
three
```
### Shell Options

Use the `set` builtin to toggle behavior. `set -e` exits on command failure, `set -u` errors on undefined variables, `set -x` prints each command before execution, `set -v` echoes input lines as they are read, `set -n` parses commands without running them, `set -f` disables wildcard expansion (use `set +f` to re-enable), `set -C` prevents `>` from overwriting existing files (use `set +C` to allow clobbering again), `set -a` exports all assignments to the environment, `set -b`/`set +b` enable or disable background job completion messages, `set -m`/`set +m` toggle job tracking, `set -t`/`set +t` exit after one command, `set -p`/`set +p` toggle privileged mode which skips startup files, `set -h`/`set +h` automatically cache commands in the hash table and `set -k`/`set +k` treat `NAME=value` after the command name as temporary environment variables.
The `set -o` form enables additional options: `pipefail` makes a pipeline return the status of the first failing command while `noclobber` (the same as `set -C`) prevents `>` from overwriting existing files. The `posix` option disables extensions such as `;&` in `case` statements, causing a syntax error if that form is used. `vi` and `emacs` select the editing mode. `ignoreeof` requires hitting `Ctrl-D` ten times to exit. Use `set +o OPTION` or `set +C` to disable an option. Invoking `set -o` or `set +o` without an argument lists all options with `on` or `off` after each name.
Use `>| file` to override `noclobber` and force truncation of `file`.

Example one-command mode:

```sh
vush> set -t
vush> echo hi
hi
```


## Built-in Commands

- `cd [-L|-P] [dir]` - change the current directory. Without an argument it switches to `$HOME`. `~user` names are expanded using the password database. After a successful change `PWD` and `OLDPWD` are updated. Use `cd -` to print and switch to `$OLDPWD`. `-L` (default) keeps `PWD` as the logical path while `-P` resolves the target with `realpath()` and sets `PWD` to the physical location. If `dir` does not begin with `/` or `.`, each directory listed in the `CDPATH` environment variable is searched. When a `CDPATH` entry is used the resulting path is printed.
  Logical canonicalization processes at most `PATH_MAX/2` components.
- `pushd dir` - push the current directory and change to `dir`.
- `popd` - return to the directory from the stack.
- `printf FORMAT [args...]` - print formatted text. Backslash escapes in
  `FORMAT` are translated before examining `%` conversions. The `%b`
  conversion interprets escapes in its argument before printing.
- `echo [-n] [-e] [args...]` - print arguments separated by spaces. With `-n` no newline is added and `-e` enables backslash escapes.
- `dirs` - display the directory stack.
- `exit [status]` - terminate the shell with an optional status code.
- `:` - do nothing and return success.
- `true` - return a successful status.
- `false` - return a failure status.
- `exec command [args...]` - replace the shell with `command`.
- `pwd [-L|-P]` - print the current working directory. `-P` displays the
  physical directory from `getcwd()` while `-L` (the default) prints `$PWD`.
- `jobs [-l|-p] [-r|-s] [ID]` - list background jobs. `-l` prints the PID and status, `-p` prints only the PID, `-r` shows running jobs only and `-s` lists only stopped jobs. With IDs only those jobs are shown.
- `fg [ID]` - wait for background job `ID` or the most recent job when omitted.
- `bg [ID]` - resume the specified job or the last started job if no ID is given.
  Job IDs may also be written as `%N`, `%%`/`%+` for the current job, `%-` for
  the previous job and `%?text` to match a command containing `text`.
- `kill [-s SIGNAL|-SIGNAL] [-l] ID|PID` - send a signal to the given job or
  process. Use `-l` to list signals or `-l NUM` to print the signal name for
  `NUM`.
- `wait [ID|PID]` - wait for the given job or process to finish.
- `trap [-p [SIGNAL]|-l | 'cmd' SIGNAL]` - execute `cmd` when `SIGNAL` is received. List traps with `-p` or no arguments, use `-p SIGNAL` to show a single trap, and `-l` to display available signals. Use `trap SIGNAL` to clear. Use `EXIT` or `0` for a command run when the shell exits.
- `export [-p|-n NAME] NAME[=VALUE]` - manage exported variables or set one.
  Use `-p` to list all exported variables. `-n NAME` stops exporting `NAME`
  without removing it. Without `=VALUE` the variable's current value is
  exported and it is created with an empty value if undefined.
- `readonly [-p] NAME[=VALUE]` - mark variables as read-only or list them.
  Without `=VALUE` the variable is created with an empty value if undefined.
  With `-p` the variables are printed using `readonly NAME=value` format.
- `local NAME[=VALUE]` - define a variable scoped to the current function.
- `unset [-f|-v] NAME` - remove functions with `-f`, variables with `-v`, or both.
- `history [-c|-d NUMBER]` - show command history, clear it with `-c`, or delete a specific entry with `-d`.
  Entries are read from and written to the file specified by `VUSH_HISTFILE`
  (default `~/.vush_history`). History size is controlled by the
  `VUSH_HISTSIZE` environment variable (default 1000).
- `fc [-lnr] [-e editor] [first [last]]` - list or edit previous commands.
  Use `-s [old=new] [command]` to immediately run a command with optional
  text replacement.
- `hash [-r] [name...]` - manage cached command paths.
- `alias [-p] [NAME[=VALUE]]` - set or display aliases. With no arguments all aliases are listed. A single NAME prints that alias. `-p` lists using `alias NAME='value'` format.
- `unalias [-a] NAME` - remove aliases. With `-a` all aliases are cleared.
- `read [-r] [-a NAME] [-p prompt] [-n nchars] [-s] [-t timeout] [-u fd] [VAR...]` -
  read a line from input. `-a` stores fields in array `NAME`, `-p` displays a
  prompt, `-n` reads up to `nchars` characters, `-s` disables echo, `-t` sets a
  timeout in seconds, and `-u` reads from the specified file descriptor. The
  input is split using the first character of `$IFS`.
- `return [status]` - return from a shell function with an optional status.
- `shift [N]` - drop the first `N` positional parameters (default 1).
- `break [N]` - exit `N` levels of loops (default 1).
- `continue [N]` - start the next iteration of the `N`th enclosing loop (default 1).
- `getopts OPTSTRING VAR` - parse positional parameters, storing the
  current option letter in `VAR`, any argument in `OPTARG`, and advancing
  `OPTIND`. When `OPTERR` is set to `0` the call behaves as if
  `OPTSTRING` began with `:` and no error messages are printed.
- `let EXPR` - evaluate an arithmetic expression.
- `(( EXPR ))` - evaluate an arithmetic expression. The exit status is 0 when
  the value is non-zero and 1 otherwise.
- `set [options] [-- args...]` - set shell options or replace positional parameters.
- `set` with no operands lists all shell variables and functions.
- `test EXPR` or `[ EXPR ]` - evaluate a conditional expression.  Supports
  string comparisons, numeric operators and POSIX unary file tests such as
  `-e`, `-f`, `-d`, `-r`, `-w`, `-x`, `-b`, `-c`, `-p`, `-h`/`-L`, `-s`, `-O`,
  `-G`, `-u`, `-g`, `-k`, `-S` and `-t`. The unary `!` operator and binary
  `-a`/`-o` apply with the usual precedence. Binary comparisons `file1 -nt file2`, `file1 -ot file2` and `file1 -ef file2` are also available.
- `[[ EXPR ]]` - evaluate a conditional expression with pattern matching.
- Aliases are stored in the file specified by `VUSH_ALIASFILE` (default
  `~/.vush_aliases`).
  The file contains one `name=value` pair per line without quotes.
- `type [-t] NAME...` - display how each NAME would be interpreted. With `-t`
  only the classification (`alias`, `function`, `builtin`, `file` or `not found`)
  is printed.
- `command [-p] [-v|-V] NAME [args...]` - run `NAME` ignoring shell functions.
  With `-v` or `-V` display how the name would be resolved. The `-p` option searches or executes using `/bin:/usr/bin` instead of the current `$PATH`.

- `eval WORDS...` - concatenate arguments and execute the result.
- `source file [args...]` or `. file [args...]` - execute commands from a file with optional positional parameters. If `file` contains no `/`, each directory in `$PATH` is searched.
- `help` - display information about built-in commands.
- `time [-p] command [args...]` - run a command and print timing statistics. With `-p`, output follows the POSIX `real`, `user`, `sys` format. Placing `time` before a pipeline times the entire sequence.
```sh
time ls | wc
```
- `times` - print cumulative user/system CPU times.
- `ulimit [-HS] [-a|-c|-d|-f|-m|-n|-s|-t|-u|-v [limit]]` - display or set resource limits.
- `umask [-S] [mask]` - set or display the file creation mask. `mask` may be an octal number or a symbolic string like `u=rwx,g=rx,o=rx`. With `-S`, the mask is shown in symbolic form.

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
vush> echo nothing >&-
vush> ls nonexistent 2>&-
vush> echo fd example 3>fd3.txt >&3
vush> cat fd3.txt
fd example
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

## Ulimit Example

```
vush> ulimit -S -s
8192
vush> ulimit -S -s 1024
vush> ulimit -H -n 4096
vush> ulimit -S -m
unlimited
vush> ulimit -S -u
unlimited
vush> ulimit -a | head -n 3
-c 0
-d unlimited
-f unlimited
```

## Conditionals and Loops

```
vush> if test 1 -eq 1; then echo yes; else echo no; fi
yes
vush> x=foo; if [[ $x == f* ]]; then echo match; fi
match
vush> for x in a b c; do echo $x; done
a
b
c
vush> export i=0; while test $i -lt 2; do echo $i; export i=$(expr $i + 1); done
0
1
vush> for x in a b c; do if test $x = b; then continue; fi; echo $x; done
a
c
vush> for ((i=0; i<3; i++)); do echo $i; done
0
1
2
vush> j=2; until test $j -eq 0; do echo $j; j=$(expr $j - 1); done
2
1
vush> if test -h /bin/sh; then echo link; fi
link
vush> test file1 -nt file2 && echo newer
vush> test file1 -ef link && echo same
vush> i=0; while true; do echo $i; break; done
0
vush> case 1 in 1) echo one ;& 2) echo two ;; esac
one
two
# The `;&` fall-through operator is a vush extension and causes a syntax error when `set -o posix` is enabled.
vush> select x in foo bar; do echo $x; break; done
1) foo
2) bar
? 2
bar
```

## Function Example

```
vush> greet() { echo Hello $1; }
vush> greet world
Hello world
vush> function bye { echo Bye $1; }
vush> bye folks
Bye folks
vush> greet foo; echo $?
Hello foo
0
```

## Read Example

Create a small script that reads a line and prints it back. The input is
split into fields using the first character of `$IFS`:

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

When no variable name is given the input goes into `$REPLY`:

```sh
read
echo "You typed: $REPLY"
```

## Type Example

```
vush> alias ll='ls -l'
vush> type ll cd ls unknown
ll is an alias for 'ls -l'
cd is a builtin
ls is /bin/ls
unknown not found
vush> type -t ll cd ls unknown
alias
builtin
file
not found
```

## Trap Example

```
vush> trap 'echo INT received' INT
vush> # press Ctrl-C to test
INT received
vush> trap 'echo exiting' EXIT
vush> trap
trap 'echo INT received' INT
trap 'echo exiting' EXIT
vush> exit
exiting
```

## Eval Example

```
vush> export cmd='echo hi'
vush> eval $cmd there
hi there
```

## Printf Example

```
vush> printf "%s %04d %x\n" foo 5 255
foo 0005 ff
vush> printf "%b" "one\\ntwo"
one
two
```

## Arithmetic Example

Numbers inside arithmetic expressions may begin with `<base>#` to specify
bases 2–36.
Supported operators include `+`, `-`, `*`, `/` and `%` along with shifts
`<<` and `>>` and bitwise `&`, `^` and `|`. Comparison operators `==`,
`!=`, `>=`, `<=`, `>` and `<` yield `1` when true and `0` otherwise.

```sh
vush> echo $((16#ff + 2#10))
257
```

## Configuration

### Environment Variables
 - `PS1` sets the prompt before each command (default `vush> `) and may be exported safely.
- `PS2` appears when more input is needed such as after an unclosed quote (default `> `).
- `PS3` is used by the `select` builtin when prompting for a choice.
- `PS4` prefixes tracing output produced by `set -x`.
- `MAIL` names a mailbox file checked before each prompt. A notice is printed
  when the file's modification time increases.
- `MAILPATH` is a `:` separated list of mailbox files also checked. Each path
  prints `New mail in <file>` when updated. Memory used to track mailbox
  modification times is freed when the shell exits.
- `OPTERR` set to `0` disables `getopts` error messages and treats missing
  arguments as if the option string started with `:`.
- `VUSH_HISTFILE` names the history file; `VUSH_HISTSIZE` limits retained entries (defaults `~/.vush_history` and `1000`).
- `VUSH_ALIASFILE` and `VUSH_FUNCFILE` store persistent aliases and functions (defaults `~/.vush_aliases` and `~/.vush_funcs`).
- `CDPATH` lists directories searched by `cd` for relative paths.
- `SHELL` holds the path used to invoke `vush`.
- `ENV` names an optional startup file read after `~/.vushrc`.
- `set -p` can be used to skip these startup files when a clean
  environment is required.

Example configuration:

```sh
# Use a custom alias file
export VUSH_ALIASFILE=~/.config/vush/aliases

# Include the working directory in the prompt
export PS1='\w> '

# Search these directories when changing directories
export CDPATH=~/projects:/tmp
cd repo    # jumps to ~/projects/repo if it exists

# Keep only 200 history entries
export VUSH_HISTSIZE=200

# Store functions in another location
export VUSH_FUNCFILE=~/.config/vush/functions
```


## Files
- `~/.vushrc` - commands executed before the first prompt if present.
- `~/.vush_history` - persistent command history.
- `~/.vush_aliases` - stored aliases.
- `~/.vush_funcs` - stored functions.
