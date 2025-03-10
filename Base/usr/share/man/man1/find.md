## Name

find - recursively search for files

## Synopsis

```**sh
$ find [-L] [root-paths...] [commands...]
```

## Description

`find` recursively traverses the file hierarchy starting at the given root paths
(or at the current working directory if no root paths have been specified), and
evaluates the given commands for each found file. The commands can be used to
both filter the set of files and to perform actions on them.

If no *action command* (`-print`, `-print0`, or `-exec`) is found among the
specified commands, a `-print` command is implicitly appended.

## Options

* `-L`: Follow symlinks

## Commands

* `-type t`: Checks if the file is of the specified type, which must be one of
  `b` (for block device), `c` (character device), `d` (directory), `l` (symbolic
  link), `p` (FIFO), `f` (regular file), and `s` (socket).
* `-links number`: Checks if the file has the given number of hard links.
* `-user name`: Checks if the file is owned by the given user. Instead of a user
  name, a numerical UID may be specified.
* `-group name`: Checks if the file is owned by the given group. Instead of a
  group name, a numerical GID may be specified.
* `-size number[bcwkMG]`: Checks if the file uses the specified `n` units of
space rounded up to the nearest whole unit.
  
  The unit of space may be specified by any of these suffixes:

  * `b`: 512-byte blocks. This is the default unit if no suffix is used.
  * `c`: bytes
  * `w`: two-byte words
  * `k`: kibibytes (1024 bytes)
  * `M`: mebibytes (1024 kibibytes)
  * `G`: gibibytes (1024 mebibytes)

* `-name pattern`: Checks if the file name matches the given global-style
  pattern (case sensitive).
* `-iname pattern`: Checks if the file name matches the given global-style
  pattern (case insensitive).
* `-readable`: Checks if the file is readable by the current user.
* `-writable`: Checks if the file is writable by the current user.
* `-executable`: Checks if the file is executable, or directory is searchable,
by the current user.
* `-print`: Outputs the file path, followed by a newline. Always evaluates to
  true.
* `-print0`: Outputs the file path, followed by a zero byte. Always evaluates to
  true.
* `-exec command... ;`: Executes the given command with any arguments provided,
  substituting the file path for any arguments specified as `{}`. The list of
  arguments must be terminated by a semicolon. Checks if the command exits
  successfully.

The commands can be combined to form complex expressions using the following
operators:

* `command1 -o command2`: Logical OR.
* `command1 -a command2`, `command1 command2`: Logical AND.
* `( command )`: Groups commands together for operator priority purposes.

## Examples

```sh
# Output a tree of paths rooted at the current directory:
$ find
# Output only directories:
$ find -type d
# Remove all sockets and any files owned by anon in /tmp:
$ find /tmp "(" -type s -o -user anon ")" -exec rm "{}" ";"
# Concatenate files with weird characters in their names:
$ find -type f -print0 | xargs -0 cat
# Find files with the word "config" in their name:
$ find -name \*config\*
```

## See also

* [`xargs`(1)](help://man/1/xargs)
