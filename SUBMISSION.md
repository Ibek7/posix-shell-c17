Submission notes for posix-shell-c17

Files to submit:
- `myshell.c` (source)
- `Makefile`
- `README.md` (features, build, usage)
- `COLLABORATION.txt`

How to build and run:
1. Run `make` to build the `myshell` binary.
2. Run `./myshell` to start the interactive shell.

Automated test:
- `make test` will run a small smoke test and print output.

Implemented features (summary):
- External command execution via fork+execvp
- Builtins: `cd`, `exit`, `jobs`, `fg`, `bg`
- Background jobs with `&`
- Single pipe support, basic I/O redirection
- Variable expansion: `$VAR`, `${VAR:-default}`, `$$`, `$?`
- Basic job control and signal handling

Known limitations
- Quoting/escaping is basic.
- No complex shell grammar support.

Contact: Ibek7 (repository owner)
