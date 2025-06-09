# Changelog

All notable changes to **vush** will be documented in this file.

## 1.0.0 - Feature complete shell
- `alias` and `unalias` builtins with definitions persisted to `~/.vush_aliases`
- Shell functions stored in `~/.vush_funcs`
- Added `read`, `trap`, `eval` and POSIX `getopts` builtins
- `case` selection statements and other control structures
- Command substitution, arithmetic expansion and here-doc support
- Line editor with history search, tab completion and customizable `PS1`
- Option parsing via `set`, persistent variables and `shift`

## 0.4.0 - Interactive editing and alias persistence
- New builtins: `unset`, `pushd`, `popd`, `dirs`, `bg`, `type`
- `~user` expansion and descriptor duplication like `2>&1`
- Persistent command history read from `~/.vushrc`
- Dynamic aliases saved to `~/.vush_aliases`
- Prompt customization via `PS1`

## 0.3.0 - Added kill builtin
- New `kill` command allows sending signals to background jobs

## 0.2.0 - Added globbing support
- Unquoted `*` and `?` patterns now expand to matching files

## 0.1.0 - Initial release
- Basic command execution and job control
- Builtin commands: `cd`, `exit`, `pwd`, `jobs`
- Quoting and environment variable expansion
- `Makefile` for building the shell
