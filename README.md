myshell - simple POSIX shell

Features:
- Interactive prompt showing current working directory
- Execute external programs using fork+execvp
- Builtins: cd, exit
- Basic I/O redirection: >, >>, < (for single commands)
- Single pipe support: cmd1 | cmd2 (no redirection mixing)
 - Background jobs with `&`, `jobs` builtin, and `fg` builtin

Build:
Run `make` in the project directory to build `myshell`.

Run:
Start the shell with `./myshell`.

Examples:
- ls -l /tmp
- cat file.txt | grep hello
- sort < unsorted.txt > sorted.txt
- cd /path
- exit

Submission checklist
--------------------
- [ ] Source files: `myshell.c`, `Makefile` included
- [ ] README describes features, build, and usage
- [ ] Collaboration statement present in `COLLABORATION.txt`
- [ ] Project builds with `make` on a POSIX system

Known limitations
-----------------
- No shell variable expansion (e.g., `$PWD`) — values are not substituted
- Limited quoting/escaping support
- Redirection inside pipelines and complex compound commands are not fully supported
- `fg` expects a numeric job id (e.g., `fg 1`); it does not accept `%1` yet

If you want improvements before submitting (variable expansion, improved parsing, bg builtin), tell me which and I'll implement them.

Collaboration statement:
I worked alone on this project.

Note about heredoc / pasted prompts:
----------------------------------
This shell intentionally does not try to guess or strip terminal prompt text that
might appear when you paste heredoc-style input (for example lines that begin
with something like "cmdand heredoc> "). If you paste such prompt text into a
heredoc or redirected input, the shell may try to execute those prompt fragments
as commands. To avoid this, paste only the raw commands (without your terminal's
prompt), or type the commands directly when using heredocs.
