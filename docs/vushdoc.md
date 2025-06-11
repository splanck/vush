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
expanding variables. Use a backslash to escape the next character. A `#` that
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
vush> false
vush> echo $?
1
vush> true
vush> echo $?
0
vush> echo $(echo hi)
hi
```

Additional parameter expansion forms (doubling `#` or `%` removes the
longest matching prefix or suffix):

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
specified range while `fc -e editor` opens the commands in `editor` and then
executes them.

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

Use the `set` builtin to toggle behavior. `set -e` exits on command failure, `set -u` errors on undefined variables, `set -x` prints each command before execution, `set -n` parses commands without running them, `set -f` disables wildcard expansion (use `set +f` to re-enable) and `set -a` exports all assignments to the environment.
The `set -o` form enables additional options: `pipefail` makes a pipeline return the status of the first failing command while `noclobber` prevents `>` from overwriting existing files. Use `set +o OPTION` to disable an option.


## Built-in Commands

- `cd [-L|-P] [dir]` - change the current directory. Without an argument it switches to `$HOME`. `~user` names are expanded using the password database. After a successful change `PWD` and `OLDPWD` are updated. Use `cd -` to print and switch to `$OLDPWD`. `-L` (default) keeps `PWD` as the logical path while `-P` resolves the target with `realpath()` and sets `PWD` to the physical location. If `dir` does not begin with `/` or `.`, each directory listed in the `CDPATH` environment variable is searched. When a `CDPATH` entry is used the resulting path is printed.
- `pushd dir` - push the current directory and change to `dir`.
- `popd` - return to the directory from the stack.
- `printf FORMAT [args...]` - print formatted text.
- `echo [-n] [-e] [args...]` - print arguments separated by spaces. With `-n` no newline is added and `-e` enables backslash escapes.
- `dirs` - display the directory stack.
- `exit [status]` - terminate the shell with an optional status code.
- `:` - do nothing and return success.
- `true` - return a successful status.
- `false` - return a failure status.
- `exec command [args...]` - replace the shell with `command`.
- `pwd [-L|-P]` - print the current working directory. `-P` displays the
  physical directory from `getcwd()` while `-L` (the default) prints `$PWD`.
- `jobs` - list background jobs started with `&`.
- `fg ID` - wait for background job `ID`.
- `bg ID` - resume a stopped background job `ID`.
- `kill [-SIGNAL] ID` - send a signal to the background job `ID`.
- `wait [ID|PID]` - wait for the given job or process to finish.
- `trap 'cmd' SIGNAL` - execute `cmd` when `SIGNAL` is received. Use `trap SIGNAL` to clear. Use `EXIT` or `0` for a command run when the shell exits.
- `export NAME=value` - set an environment variable for the shell.
- `readonly NAME[=VALUE]` - mark a variable as read-only.
- `local NAME[=VALUE]` - define a variable scoped to the current function.
- `unset [-f] NAME` - remove an environment variable or function with `-f`.
- `history [-c|-d NUMBER]` - show command history, clear it with `-c`, or delete a specific entry with `-d`.
  Entries are read from and written to the file specified by `VUSH_HISTFILE`
  (default `~/.vush_history`). History size is controlled by the
  `VUSH_HISTSIZE` environment variable (default 1000).
- `fc [-l] [-e editor] [first [last]]` - list or edit previous commands.
- `hash [-r] [name...]` - manage cached command paths.
- `alias NAME=value` - define an alias or list all aliases when used without arguments.
- `unalias NAME` - remove an alias.
- `read [-r] VAR...` - read a line of input into variables.
- `return [status]` - return from a shell function with an optional status.
- `shift [N]` - drop the first `N` positional parameters (default 1).
- `break [N]` - exit `N` levels of loops (default 1).
- `continue [N]` - start the next iteration of the `N`th enclosing loop (default 1).
- `getopts OPTSTRING VAR` - parse positional parameters, storing the
  current option letter in `VAR`, any argument in `OPTARG`, and advancing
  `OPTIND`.
- `let EXPR` - evaluate an arithmetic expression.
- `set [options] [-- args...]` - set shell options or replace positional parameters.
- `test EXPR` or `[ EXPR ]` - evaluate a conditional expression.
- `[[ EXPR ]]` - evaluate a conditional expression with pattern matching.
- Aliases are stored in the file specified by `VUSH_ALIASFILE` (default
  `~/.vush_aliases`).
  The file contains one `name=value` pair per line without quotes.
- `type NAME...` - display how each NAME would be interpreted.
- `command NAME [args...]` - execute a command without shell function lookup.
- `eval WORDS...` - concatenate arguments and execute the result.
- `source file [args...]` or `. file [args...]` - execute commands from a file with optional positional parameters.
- `help` - display information about built-in commands.
- `time command [args...]` - run a command and print timing statistics.
- `times` - print cumulative user/system CPU times.
- `ulimit [-a|-f|-n [limit]]` - display or set resource limits.
- `umask [mask]` - set or display the file creation mask.

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
vush> i=0; while true; do echo $i; break; done
0
vush> case 1 in 1) echo one ;& 2) echo two ;; esac
one
two
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

## Trap Example

```
vush> trap 'echo INT received' INT
vush> # press Ctrl-C to test
INT received
vush> trap 'echo exiting' EXIT
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
```

## Configuration

Several environment variables and a startup file influence the shell:

- `PS1` sets the interactive prompt and is expanded like normal text. The
  default is `vush> `.
- `PS2` is displayed when additional input is required, for example after an
  unclosed quote. The default is `> `.
- `PS3` is shown by the `select` builtin when prompting for a choice.
- `PS4` prefixes tracing output when `set -x` is enabled.
- `VUSH_HISTFILE` names the history file while `VUSH_HISTSIZE` limits how many
  entries are retained. Defaults are `~/.vush_history` and `1000`.
- `VUSH_ALIASFILE` and `VUSH_FUNCFILE` store persistent aliases and shell
  functions. They default to `~/.vush_aliases` and `~/.vush_funcs`.
- `CDPATH` provides a colon-separated list of directories searched by `cd` for
  relative paths.
- `~/.vushrc` is executed before the first prompt if it exists.

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

By default history is saved to `~/.vush_history`, aliases to `~/.vush_aliases`,
functions to `~/.vush_funcs`, and startup commands are read from `~/.vushrc`.
See the manual page for more detail.

