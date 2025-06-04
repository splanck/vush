# Changelog

All notable changes to **vush** will be documented in this file.

## 0.3.0 - Added kill builtin
- New `kill` command allows sending signals to background jobs

## 0.2.0 - Added globbing support
- Unquoted `*` and `?` patterns now expand to matching files

## 0.1.0 - Initial release
- Basic command execution and job control
- Builtin commands: `cd`, `exit`, `pwd`, `jobs`
- Quoting and environment variable expansion
- `Makefile` for building the shell
