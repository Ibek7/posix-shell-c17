/*
 * Simple POSIX shell (myshell.c)
 * Features:
 *  - Read-eval loop with prompt
 *  - Parse command line into arguments
 *  - Builtins: cd, exit
 *  - Launch external programs with fork+execvp
 *  - Basic I/O redirection: >, >>, <
 *  - Single pipe support: cmd1 | cmd2
 *
 * Compile: make
 * Usage: ./myshell
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>

#define MAX_TOKENS 128
#define MAX_LINE 4096
#define MAX_JOBS 64

typedef enum { JOB_RUNNING = 0, JOB_DONE = 1, JOB_STOPPED = 2 } job_status_t;

struct job {
    int jid;
    pid_t pid;
    job_status_t status;
    char *cmdline;
};

static struct job jobs[MAX_JOBS];
static int next_jid = 1;
static int last_status = 0; // $? value

static int add_job(pid_t pid, const char *cmdline) {
    for (int i = 0; i < MAX_JOBS; ++i) {
        if (jobs[i].pid == 0) {
            jobs[i].pid = pid;
            jobs[i].jid = next_jid++;
            jobs[i].status = JOB_RUNNING;
            jobs[i].cmdline = strdup(cmdline ? cmdline : "");
            return jobs[i].jid;
        }
    }
    return -1; // no space
}

static struct job *find_job_by_pid(pid_t pid) {
    for (int i = 0; i < MAX_JOBS; ++i) if (jobs[i].pid == pid) return &jobs[i];
    return NULL;
}

static struct job *find_job_by_jid(int jid) {
    for (int i = 0; i < MAX_JOBS; ++i) if (jobs[i].jid == jid) return &jobs[i];
    return NULL;
}

static void cleanup_jobs(void) {
    for (int i = 0; i < MAX_JOBS; ++i) {
        if (jobs[i].pid != 0 && jobs[i].status == JOB_DONE) {
            // remove completed jobs from table
            remove_job(&jobs[i]);
        }
    }
}

static void remove_job(struct job *j) {
    if (!j) return;
    free(j->cmdline);
    j->cmdline = NULL;
    j->pid = 0; j->jid = 0; j->status = JOB_DONE;
}

// SIGCHLD handler: reap children and update job table
static void sigchld_handler(int sig) {
    (void)sig;
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        struct job *j = find_job_by_pid(pid);
        if (!j) continue;
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            j->status = JOB_DONE;
        } else if (WIFSTOPPED(status)) {
            j->status = JOB_STOPPED;
        } else if (WIFCONTINUED(status)) {
            j->status = JOB_RUNNING;
        }
    }
}

// Wrapper for waitpid that only prints errors when they are not ECHILD
static pid_t safe_waitpid(pid_t pid, int *status, int options) {
    pid_t r = waitpid(pid, status, options);
    if (r == -1 && errno == ECHILD) {
        // no child processes - suppress noisy perror
        return -1;
    }
    if (r == -1) perror("waitpid");
    return r;
}

// Expand environment variables in `in` into `out`. Supports $VAR and ${VAR}.
// Does not expand inside single quotes. Expands inside double quotes.
static void expand_variables(const char *in, char *out, size_t outsz) {
    size_t oi = 0;
    int in_single = 0;
    int in_double = 0;
    for (size_t i = 0; in[i] != '\0' && oi + 1 < outsz; ++i) {
        char c = in[i];
        if (c == '\'' && !in_double) {
            in_single = !in_single;
            out[oi++] = c;
            continue;
        }
        if (c == '"' && !in_single) {
            in_double = !in_double;
            out[oi++] = c;
            continue;
        }
        if (c == '$' && !in_single) {
            // Special variables
            if (in[i+1] == '$') {
                char buf[32];
                snprintf(buf, sizeof(buf), "%d", getpid());
                size_t k = 0;
                while (buf[k] && oi + 1 < outsz) out[oi++] = buf[k++];
                i++; // skip the second $
                continue;
            }
            if (in[i+1] == '?') {
                char buf[32];
                snprintf(buf, sizeof(buf), "%d", last_status);
                size_t k = 0;
                while (buf[k] && oi + 1 < outsz) out[oi++] = buf[k++];
                i++;
                continue;
            }
            // Handle ${VAR}
            if (in[i+1] == '{') {
                size_t j = i+2;
                // parse name
                while (in[j] && (isalnum((unsigned char)in[j]) || in[j] == '_')) j++;
                size_t namelen = j - (i+2);
                char name[256] = {0};
                if (namelen >= sizeof(name)) namelen = sizeof(name)-1;
                memcpy(name, &in[i+2], namelen);
                // handle default ${VAR:-default}
                if (in[j] == ':' && in[j+1] == '-') {
                    size_t kpos = j+2;
                    size_t kend = kpos;
                    while (in[kend] && in[kend] != '}') kend++;
                    size_t deflen = (kend > kpos) ? (kend - kpos) : 0;
                    char defbuf[MAX_LINE];
                    if (deflen > 0) {
                        if (deflen >= sizeof(defbuf)) deflen = sizeof(defbuf)-1;
                        memcpy(defbuf, &in[kpos], deflen);
                        defbuf[deflen] = '\0';
                    } else defbuf[0] = '\0';
                    char defexp[MAX_LINE];
                    expand_variables(defbuf, defexp, sizeof(defexp));
                    char *val = getenv(name);
                    if (val && val[0] != '\0') {
                        size_t kk = 0; while (val[kk] && oi + 1 < outsz) out[oi++] = val[kk++];
                    } else {
                        size_t kk = 0; while (defexp[kk] && oi + 1 < outsz) out[oi++] = defexp[kk++];
                    }
                    // skip past }
                    if (in[kend] == '}') i = kend; else i = j;
                    continue;
                }
                if (in[j] == '}') {
                    size_t len = j - (i+2);
                    char name2[256];
                    if (len >= sizeof(name2)) len = sizeof(name2)-1;
                    memcpy(name2, &in[i+2], len);
                    name2[len] = '\0';
                    char *val = getenv(name2);
                    if (val) {
                        size_t k = 0;
                        while (val[k] && oi + 1 < outsz) out[oi++] = val[k++];
                    }
                    i = j; // skip past }
                    continue;
                }
            }
            // Handle $VAR (letters, digits, underscore)
            if (isalpha((unsigned char)in[i+1]) || in[i+1] == '_') {
                size_t j = i+1;
                while (in[j] && (isalnum((unsigned char)in[j]) || in[j] == '_')) j++;
                size_t len = j - (i+1);
                char name[256];
                if (len >= sizeof(name)) len = sizeof(name)-1;
                memcpy(name, &in[i+1], len);
                name[len] = '\0';
                char *val = getenv(name);
                if (val) {
                    size_t k = 0;
                    while (val[k] && oi + 1 < outsz) out[oi++] = val[k++];
                }
                i = j-1;
                continue;
            }
            // otherwise copy '$'
            out[oi++] = '$';
            continue;
        }
        out[oi++] = c;
    }
    out[oi] = '\0';
}

// Split a string into tokens (whitespace separated). Returns token count.
static int tokenize(char *line, char **tokens) {
    int n = 0;
    char *p = line;
    while (*p && n < MAX_TOKENS - 1) {
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n')) p++;
        if (!*p) break;
        if (*p == '"' || *p == '\'') {
            char quote = *p++;
            tokens[n++] = p;
            while (*p && *p != quote) p++;
            if (*p) *p++ = '\0';
        } else {
            tokens[n++] = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
            if (*p) *p++ = '\0';
        }
    }
    tokens[n] = NULL;
    return n;
}

// Find token index of a string or -1
// (Unused helper functions removed)

// Handle a pipeline of two commands: left | right
static int exec_pipe(char **left, char **right) {
    int pipefd[2];
    if (pipe(pipefd) < 0) { perror("pipe"); return -1; }
    pid_t p1 = fork();
    if (p1 < 0) { perror("fork"); return -1; }
    if (p1 == 0) {
        // left child -> write end
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) { perror("dup2"); _exit(1); }
        close(pipefd[1]);
        // restore default signal handlers in child so it responds to Ctrl-C
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        execvp(left[0], left);
        fprintf(stderr, "myshell: exec failed: %s: %s\n", left[0], strerror(errno));
        _exit(127);
    }

    pid_t p2 = fork();
    if (p2 < 0) { perror("fork"); return -1; }
    if (p2 == 0) {
        // right child -> read end
        close(pipefd[1]);
        if (dup2(pipefd[0], STDIN_FILENO) < 0) { perror("dup2"); _exit(1); }
        close(pipefd[0]);
        // restore default signal handlers in child so it responds to Ctrl-C
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        execvp(right[0], right);
        fprintf(stderr, "myshell: exec failed: %s: %s\n", right[0], strerror(errno));
        _exit(127);
    }

    close(pipefd[0]); close(pipefd[1]);
    int status;
    if (safe_waitpid(p1, &status, 0) > 0) last_status = WEXITSTATUS(status);
    if (safe_waitpid(p2, &status, 0) > 0) last_status = WEXITSTATUS(status);
    return status;
}

// Process redirection tokens in argv in-place. Returns 0 on success.
static int handle_redirection(char **argv) {
    for (int i = 0; argv[i]; ++i) {
        if (strcmp(argv[i], ">") == 0 || strcmp(argv[i], ">>") == 0) {
            int append = (strcmp(argv[i], ">>") == 0);
            char *filename = argv[i+1];
            if (!filename) { fprintf(stderr, "myshell: missing filename for redirection\n"); return -1; }
            int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
            int fd = open(filename, flags, 0644);
            if (fd < 0) { fprintf(stderr, "myshell: open '%s': %s\n", filename, strerror(errno)); return -1; }
            if (dup2(fd, STDOUT_FILENO) < 0) { perror("dup2"); close(fd); return -1; }
            close(fd);
            // remove tokens i and i+1
            for (int j = i; argv[j+2]; ++j) argv[j] = argv[j+2];
            argv[i] = NULL; // ensure termination
            i--; // recheck current index
        } else if (strcmp(argv[i], "<") == 0) {
            char *filename = argv[i+1];
            if (!filename) { fprintf(stderr, "myshell: missing filename for input redirection\n"); return -1; }
            int fd = open(filename, O_RDONLY);
            if (fd < 0) { fprintf(stderr, "myshell: open '%s': %s\n", filename, strerror(errno)); return -1; }
            if (dup2(fd, STDIN_FILENO) < 0) { perror("dup2"); close(fd); return -1; }
            close(fd);
            for (int j = i; argv[j+2]; ++j) argv[j] = argv[j+2];
            argv[i] = NULL;
            i--;
        }
    }
    return 0;
}

int main(void) {
    // Ignore SIGINT in the parent so Ctrl-C doesn't kill the shell; children will restore default.
    signal(SIGINT, SIG_IGN);

    // Install SIGCHLD handler to update job table
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    char line[MAX_LINE];
    char *tokens[MAX_TOKENS];

    int interactive = isatty(STDIN_FILENO);
    while (1) {
    // cleanup finished jobs periodically
    cleanup_jobs();
        // Print prompt only when interactive
        if (interactive) {
            char cwd[1024];
            if (getcwd(cwd, sizeof(cwd))) printf("%s $ ", cwd); else printf("myshell $ ");
            fflush(stdout);
        }

        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break; // EOF
        }

        // strip trailing newline
        size_t L = strlen(line);
        if (L > 0 && line[L-1] == '\n') line[L-1] = '\0';

        // If stdin is not interactive, some terminals may include a prompt string
        // in heredoc content (for example: "cmdand heredoc> echo hello").
        // Detect and strip occurrences of " heredoc> " prefixes so they
        // don't get treated as commands.
        if (!interactive) {
            const char *marker = " heredoc> ";
            char *found;
            // Look for the marker and, if found, move the remainder to the front.
            if ((found = strstr(line, marker)) != NULL) {
                char *start = found + strlen(marker);
                memmove(line, start, strlen(start) + 1);
            }
        }

    // skip empty
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') continue;

    // Expand variables in a safe buffer before tokenizing
    char expanded[MAX_LINE];
    expand_variables(line, expanded, sizeof(expanded));


        // Tokenize
    int n = tokenize(expanded, tokens);
        if (n == 0) continue;

    // ignore empty tokens that can result from lone quotes like a line with just '\''
    if (tokens[0] && tokens[0][0] == '\0') continue;

        // Strip surrounding quotes from tokens so arguments don't carry literal quotes
        for (int ti = 0; tokens[ti]; ++ti) {
            char *t = tokens[ti];
            size_t len = strlen(t);
            if (len >= 2 && ((t[0] == '"' && t[len-1] == '"') || (t[0] == '\'' && t[len-1] == '\''))) {
                // shift left and overwrite trailing quote
                memmove(t, t+1, len-2);
                t[len-2] = '\0';
            }
        }

        // builtins: exit, cd, jobs, fg
        if (strcmp(tokens[0], "exit") == 0) {
            break;
        }
        if (strcmp(tokens[0], "cd") == 0) {
            char *dir = tokens[1] ? tokens[1] : getenv("HOME");
            if (chdir(dir) < 0) perror("chdir"); else {
                char cwd[1024];
                if (getcwd(cwd, sizeof(cwd))) setenv("PWD", cwd, 1);
            }
            continue;
        }
        if (strcmp(tokens[0], "jobs") == 0) {
            for (int k = 0; k < MAX_JOBS; ++k) {
                if (jobs[k].pid != 0) {
                    printf("[%d] %s %s\n", jobs[k].jid,
                           jobs[k].status == JOB_RUNNING ? "Running" : jobs[k].status == JOB_STOPPED ? "Stopped" : "Done",
                           jobs[k].cmdline);
                }
            }
            continue;
        }
        if (strcmp(tokens[0], "fg") == 0) {
            if (!tokens[1]) { fprintf(stderr, "fg: usage: fg %%jid\n"); continue; }
            char *arg = tokens[1];
            if (arg[0] == '%') arg++;
            int jid = atoi(arg);
            struct job *j = find_job_by_jid(jid);
            if (!j) { fprintf(stderr, "fg: no such job %d\n", jid); continue; }
            // bring to foreground
            kill(j->pid, SIGCONT);
            int st; if (safe_waitpid(j->pid, &st, 0) > 0) last_status = WEXITSTATUS(st);
            remove_job(j);
            continue;
        }
        if (strcmp(tokens[0], "bg") == 0) {
            if (!tokens[1]) { fprintf(stderr, "bg: usage: bg %%jid\n"); continue; }
            char *arg = tokens[1]; if (arg[0] == '%') arg++;
            int jid = atoi(arg);
            struct job *j = find_job_by_jid(jid);
            if (!j) { fprintf(stderr, "bg: no such job %d\n", jid); continue; }
            kill(j->pid, SIGCONT);
            j->status = JOB_RUNNING;
            continue;
        }

        // look for pipe
        int pipe_idx = -1;
        for (int i = 0; tokens[i]; ++i) if (strcmp(tokens[i], "|") == 0) { pipe_idx = i; break; }

        if (pipe_idx >= 0) {
            tokens[pipe_idx] = NULL;
            char **left = tokens;
            char **right = &tokens[pipe_idx+1];
            // Note: redirection inside pipeline is NOT fully supported in this simple implementation
            exec_pipe(left, right);
            continue;
        }

        // check for background '&' (last token)
        int background = 0;
        int last = 0; while (tokens[last]) last++;
        if (last > 0 && strcmp(tokens[last-1], "&") == 0) {
            background = 1;
            tokens[last-1] = NULL;
        }

        // handle redirection for single command
    // copy args to new argv array because handle_redirection modifies argv in-place
    char *argv[MAX_TOKENS];
    int i = 0;
    for (; tokens[i]; ++i) argv[i] = tokens[i];
    argv[i] = NULL;

        // Before exec, handle redirection by forking a child that sets FDs then exec
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); continue; }
        if (pid == 0) {
            // child: handle redirection tokens
            // restore default signals in child
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            handle_redirection(argv);
            if (argv[0] == NULL) _exit(0);
            execvp(argv[0], argv);
            fprintf(stderr, "myshell: exec failed: %s: %s\n", argv[0], strerror(errno));
            _exit(127);
        } else {
            if (background) {
                int jid = add_job(pid, line);
                if (jid < 0) fprintf(stderr, "myshell: job table full\n");
                else printf("[%d] %d\n", jid, pid);
            } else {
                int status;
                if (safe_waitpid(pid, &status, 0) > 0) {
                    if (WIFEXITED(status)) last_status = WEXITSTATUS(status);
                    else if (WIFSIGNALED(status)) last_status = 128 + WTERMSIG(status);
                }
            }
        }
    }

    return 0;
}
