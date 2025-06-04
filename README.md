# vush

`vush` is a simple UNIX shell written in C. It supports basic command execution
and a few built-in commands.

## Features

- Command line parsing with rudimentary quoting support
- Execution of external commands via `fork` and `exec`
- Built-in commands: `cd`, `exit`, `pwd`, and `jobs`
- Environment variable expansion for tokens beginning with `$`
- Background job management using `&`

## Building

```sh
cc -o vush src/main.c
```

## Usage

Run the `vush` binary and enter commands as you would in a normal shell.

```
./vush
vush> ls -l
vush> cd /tmp
vush> echo $HOME
vush> sleep 5 &
```
```

