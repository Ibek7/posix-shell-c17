myshell - simple POSIX shell

Features:
- Interactive prompt showing current working directory
- Execute external programs using fork+execvp
- Builtins: cd, exit
- Basic I/O redirection: >, >>, < (for single commands)
- Single pipe support: cmd1 | cmd2 (no redirection mixing)

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
