/*
 * Minimal bash-like shell for a hobby OS (newlib, no stdio).
 *
 * Uses only: read, write, open, close, getcwd, chdir, getpid, malloc (sbrk), exit.
 *
 * Supported:
 *   - prompt showing the current working directory
 *   - builtins: cd pwd echo exit export unset set read source . type help
 *               test [ true false :
 *   - variables: NAME=value, $NAME, ${NAME}, $?, $$, ~
 *   - quoting: 'single', "double", backslash
 *   - operators: ;  &&  ||   and  # comments
 *   - redirection: < > >>   (builtins now; passed to external_command() for you)
 *   - scripts: `shell file` or `source file`
 *
 * NOT supported (yet): pipes, background jobs (&), if/while/for, globbing.
 *
 * Running programs: implement external_command() at the bottom of the
 * "execution" section. Everything else is already wired up to call it.
 */

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Limits                                                              */
/* ------------------------------------------------------------------ */

#define MAX_LINE          512
#define MAX_TOKENS        128
#define MAX_ARGS          64
#define POOL_SIZE         2048   /* chars available for one expanded line */
#define MAX_VARS          64
#define VAR_NAME_MAX      32
#define VAR_VALUE_MAX     192
#define CWD_MAX           256
#define MAX_SOURCE_DEPTH  8
#define MAX_SCRIPT_SIZE   65536

/* ------------------------------------------------------------------ */
/* Global shell state                                                  */
/* ------------------------------------------------------------------ */

static int last_status = 0;     /* $?                                   */
static int should_exit = 0;     /* set by `exit`, checked by all loops  */
static int source_depth = 0;

/* Builtins write to out_fd / read from in_fd, so redirection works
 * without needing dup2(). */
static int out_fd = 1;
static int in_fd = 0;

/* ------------------------------------------------------------------ */
/* Small output helpers (no stdio)                                     */
/* ------------------------------------------------------------------ */

static void fd_puts(int fd, const char *s)
{
  size_t len = strlen(s);

  while (len > 0) {
    int n = write(fd, s, len);
    if (n <= 0)
      return;
    s += n;
    len -= (size_t)n;
  }
}

static void out_puts(const char *s) { fd_puts(out_fd, s); }
static void err_puts(const char *s) { fd_puts(2, s); }

/* Prints "bash: a b c\n" to stderr; NULL parts are skipped. */
static void err3(const char *a, const char *b, const char *c)
{
  err_puts("bash: ");
  if (a) err_puts(a);
  if (b) err_puts(b);
  if (c) err_puts(c);
  err_puts("\n");
}

static void int_to_str(int value, char *buf)
{
  char tmp[12];
  int i = 0, j = 0;
  unsigned int u = value < 0 ? 0u - (unsigned int)value : (unsigned int)value;

  do {
    tmp[i++] = (char)('0' + u % 10);
    u /= 10;
  } while (u);

  if (value < 0)
    buf[j++] = '-';
  while (i > 0)
    buf[j++] = tmp[--i];
  buf[j] = '\0';
}

/* ------------------------------------------------------------------ */
/* Variables                                                           */
/* ------------------------------------------------------------------ */

struct var {
  char name[VAR_NAME_MAX];
  char value[VAR_VALUE_MAX];
  int exported;
  int used;
};

static struct var vars[MAX_VARS];

static struct var *find_var(const char *name)
{
  for (int i = 0; i < MAX_VARS; i++)
    if (vars[i].used && strcmp(vars[i].name, name) == 0)
      return &vars[i];
  return NULL;
}

static const char *get_var(const char *name)
{
  struct var *v = find_var(name);
  return v ? v->value : NULL;
}

/* export_flag: 1 = mark exported, 0 = leave as is. Returns 0 or -1. */
static int set_var(const char *name, const char *value, int export_flag)
{
  struct var *v = find_var(name);

  if (strlen(name) >= VAR_NAME_MAX || strlen(value) >= VAR_VALUE_MAX)
    return -1;

  if (!v) {
    for (int i = 0; i < MAX_VARS; i++) {
      if (!vars[i].used) {
        v = &vars[i];
        break;
      }
    }
    if (!v)
      return -1;
    v->used = 1;
    v->exported = 0;
    strcpy(v->name, name);
  }

  strcpy(v->value, value);
  if (export_flag)
    v->exported = 1;
  return 0;
}

static void unset_var(const char *name)
{
  struct var *v = find_var(name);
  if (v)
    memset(v, 0, sizeof(*v));
}

static int is_name_start(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_name_char(char c)
{
  return is_name_start(c) || (c >= '0' && c <= '9');
}

static int is_valid_name(const char *s)
{
  if (!is_name_start(*s))
    return 0;
  for (; *s; s++)
    if (!is_name_char(*s))
      return 0;
  return 1;
}

/* ------------------------------------------------------------------ */
/* Tokenizer (quotes + expansion happen here)                          */
/* ------------------------------------------------------------------ */

enum tok_type { T_WORD, T_SEMI, T_AND, T_OR, T_IN, T_OUT, T_APPEND };

struct token {
  enum tok_type type;
  const char *text;             /* only for T_WORD, points into pool */
};

/* Heap allocated per line so `source` can nest safely. */
struct parsed {
  struct token toks[MAX_TOKENS];
  int count;
  char pool[POOL_SIZE];
  size_t pool_len;
  int overflow;
};

static void pool_add(struct parsed *P, char c)
{
  if (P->pool_len >= POOL_SIZE)
    P->overflow = 1;
  else
    P->pool[P->pool_len++] = c;
}

static void pool_add_str(struct parsed *P, const char *s)
{
  while (*s)
    pool_add(P, *s++);
}

static int add_token(struct parsed *P, enum tok_type type, const char *text)
{
  if (P->count >= MAX_TOKENS) {
    err3("line too complex (too many tokens)", NULL, NULL);
    return -1;
  }
  P->toks[P->count].type = type;
  P->toks[P->count].text = text;
  P->count++;
  return 0;
}

/* *pp points at a '$'. Appends the expansion, advances *pp past it. */
static void expand_dollar(struct parsed *P, const char **pp)
{
  const char *p = *pp + 1;
  char name[VAR_NAME_MAX];
  char num[12];
  int n = 0;
  const char *val;

  if (*p == '?') {
    int_to_str(last_status, num);
    pool_add_str(P, num);
    p++;
  } else if (*p == '$') {
    int_to_str(getpid(), num);
    pool_add_str(P, num);
    p++;
  } else if (*p == '{') {
    p++;
    while (*p && *p != '}' && n < VAR_NAME_MAX - 1)
      name[n++] = *p++;
    name[n] = '\0';
    if (*p == '}')
      p++;
    val = get_var(name);
    if (val)
      pool_add_str(P, val);
  } else if (is_name_start(*p)) {
    while (is_name_char(*p) && n < VAR_NAME_MAX - 1)
      name[n++] = *p++;
    name[n] = '\0';
    val = get_var(name);
    if (val)
      pool_add_str(P, val);
  } else {
    pool_add(P, '$');
  }

  *pp = p;
}

static int is_blank(char c) { return c == ' ' || c == '\t' || c == '\r'; }
static int is_op_char(char c) { return c && strchr(";&|<>", c) != NULL; }

/* Returns 0 on success, -1 on error (message already printed). */
static int tokenize(const char *line, struct parsed *P)
{
  const char *p = line;

  P->count = 0;
  P->pool_len = 0;
  P->overflow = 0;

  while (*p) {
    if (is_blank(*p)) { p++; continue; }
    if (*p == '#') break;

    if (*p == ';') {
      if (add_token(P, T_SEMI, NULL) < 0) return -1;
      p++;
      continue;
    }
    if (*p == '&') {
      if (p[1] != '&') {
        err3("background jobs (&) are not supported", NULL, NULL);
        return -1;
      }
      if (add_token(P, T_AND, NULL) < 0) return -1;
      p += 2;
      continue;
    }
    if (*p == '|') {
      if (p[1] != '|') {
        err3("pipes (|) are not supported", NULL, NULL);
        return -1;
      }
      if (add_token(P, T_OR, NULL) < 0) return -1;
      p += 2;
      continue;
    }
    if (*p == '<') {
      if (add_token(P, T_IN, NULL) < 0) return -1;
      p++;
      continue;
    }
    if (*p == '>') {
      if (p[1] == '>') {
        if (add_token(P, T_APPEND, NULL) < 0) return -1;
        p += 2;
      } else {
        if (add_token(P, T_OUT, NULL) < 0) return -1;
        p++;
      }
      continue;
    }

    /* A word. */
    size_t start = P->pool_len;
    int quoted = 0;

    while (*p && !is_blank(*p) && !is_op_char(*p)) {
      if (*p == '\'') {
        quoted = 1;
        p++;
        while (*p && *p != '\'')
          pool_add(P, *p++);
        if (!*p) {
          err3("unterminated single quote", NULL, NULL);
          return -1;
        }
        p++;
      } else if (*p == '"') {
        quoted = 1;
        p++;
        while (*p && *p != '"') {
          if (*p == '\\' && (p[1] == '"' || p[1] == '\\' || p[1] == '$')) {
            pool_add(P, p[1]);
            p += 2;
          } else if (*p == '$') {
            expand_dollar(P, &p);
          } else {
            pool_add(P, *p++);
          }
        }
        if (!*p) {
          err3("unterminated double quote", NULL, NULL);
          return -1;
        }
        p++;
      } else if (*p == '\\') {
        quoted = 1;
        if (p[1]) {
          pool_add(P, p[1]);
          p += 2;
        } else {
          p++;
        }
      } else if (*p == '$') {
        expand_dollar(P, &p);
      } else if (*p == '~' && P->pool_len == start &&
                 (p[1] == '\0' || p[1] == '/' || is_blank(p[1]) || is_op_char(p[1]))) {
        const char *home = get_var("HOME");
        pool_add_str(P, home ? home : "/");
        p++;
      } else {
        pool_add(P, *p++);
      }
    }

    if (P->overflow) {
      err3("line too long after expansion", NULL, NULL);
      return -1;
    }

    /* An unquoted word that expanded to nothing disappears (like bash). */
    if (P->pool_len == start && !quoted)
      continue;

    pool_add(P, '\0');
    if (P->overflow) {
      err3("line too long after expansion", NULL, NULL);
      return -1;
    }
    if (add_token(P, T_WORD, &P->pool[start]) < 0)
      return -1;
  }

  return 0;
}

/* ------------------------------------------------------------------ */
/* Builtins                                                            */
/* ------------------------------------------------------------------ */

static int run_script(const char *path);

static int builtin_true(int argc, char **argv) { (void)argc; (void)argv; return 0; }
static int builtin_false(int argc, char **argv) { (void)argc; (void)argv; return 1; }

static int builtin_pwd(int argc, char **argv)
{
  char cwd[CWD_MAX];
  (void)argc; (void)argv;

  if (!getcwd(cwd, sizeof(cwd))) {
    err3("pwd: cannot determine current directory", NULL, NULL);
    return 1;
  }
  out_puts(cwd);
  out_puts("\n");
  return 0;
}

static int builtin_cd(int argc, char **argv)
{
  char old[CWD_MAX], now[CWD_MAX];
  const char *target;
  int print_target = 0;

  if (argc > 2) {
    err3("cd: too many arguments", NULL, NULL);
    return 1;
  }

  if (argc == 1) {
    target = get_var("HOME");
    if (!target || !*target)
      target = "/";
  } else if (strcmp(argv[1], "-") == 0) {
    target = get_var("OLDPWD");
    if (!target || !*target) {
      err3("cd: OLDPWD not set", NULL, NULL);
      return 1;
    }
    print_target = 1;
  } else {
    target = argv[1];
  }

  if (!getcwd(old, sizeof(old)))
    old[0] = '\0';

  if (chdir(target) < 0) {
    err3("cd: ", target, ": cannot change directory");
    return 1;
  }

  if (getcwd(now, sizeof(now))) {
    if (old[0])
      set_var("OLDPWD", old, 0);
    set_var("PWD", now, 0);
    if (print_target) {
      out_puts(now);
      out_puts("\n");
    }
  }
  return 0;
}

static int builtin_echo(int argc, char **argv)
{
  int i = 1, newline = 1;

  if (argc > 1 && strcmp(argv[1], "-n") == 0) {
    newline = 0;
    i = 2;
  }
  for (int first = 1; i < argc; i++, first = 0) {
    if (!first)
      out_puts(" ");
    out_puts(argv[i]);
  }
  if (newline)
    out_puts("\n");
  return 0;
}

static int builtin_exit(int argc, char **argv)
{
  if (argc > 2) {
    err3("exit: too many arguments", NULL, NULL);
    return 1;
  }
  if (argc == 2)
    last_status = (int)(strtol(argv[1], NULL, 10) & 0xff);
  should_exit = 1;
  return last_status;
}

static int builtin_export(int argc, char **argv)
{
  int status = 0;

  if (argc == 1) {
    for (int i = 0; i < MAX_VARS; i++) {
      if (vars[i].used && vars[i].exported) {
        out_puts("export ");
        out_puts(vars[i].name);
        out_puts("=\"");
        out_puts(vars[i].value);
        out_puts("\"\n");
      }
    }
    return 0;
  }

  for (int i = 1; i < argc; i++) {
    char name[VAR_NAME_MAX];
    const char *eq = strchr(argv[i], '=');
    size_t name_len = eq ? (size_t)(eq - argv[i]) : strlen(argv[i]);

    if (name_len == 0 || name_len >= VAR_NAME_MAX) {
      err3("export: invalid name: ", argv[i], NULL);
      status = 1;
      continue;
    }
    memcpy(name, argv[i], name_len);
    name[name_len] = '\0';

    if (!is_valid_name(name)) {
      err3("export: not a valid identifier: ", name, NULL);
      status = 1;
      continue;
    }

    if (eq) {
      if (set_var(name, eq + 1, 1) < 0) {
        err3("export: cannot set ", name, NULL);
        status = 1;
      }
    } else if (find_var(name)) {
      find_var(name)->exported = 1;
    } else if (set_var(name, "", 1) < 0) {
      status = 1;
    }
  }
  return status;
}

static int builtin_unset(int argc, char **argv)
{
  for (int i = 1; i < argc; i++)
    unset_var(argv[i]);
  return 0;
}

static int builtin_set(int argc, char **argv)
{
  (void)argc; (void)argv;
  for (int i = 0; i < MAX_VARS; i++) {
    if (vars[i].used) {
      out_puts(vars[i].name);
      out_puts("=");
      out_puts(vars[i].value);
      out_puts("\n");
    }
  }
  return 0;
}

static int builtin_read(int argc, char **argv)
{
  char buf[VAR_VALUE_MAX];
  size_t len = 0;
  char c;
  int got_any = 0;
  const char *name = argc > 1 ? argv[1] : "REPLY";

  if (!is_valid_name(name)) {
    err3("read: not a valid identifier: ", name, NULL);
    return 2;
  }

  for (;;) {
    int n = read(in_fd, &c, 1);
    if (n <= 0)
      break;
    got_any = 1;
    if (c == '\n')
      break;
    if (c == '\r')
      continue;
    if (len < sizeof(buf) - 1)
      buf[len++] = c;
  }
  buf[len] = '\0';

  set_var(name, buf, 0);
  return got_any ? 0 : 1;
}

static int builtin_source(int argc, char **argv)
{
  if (argc < 2) {
    err3(argv[0], ": filename argument required", NULL);
    return 2;
  }
  return run_script(argv[1]);
}

/* ---- test / [ ---- */

static int test_eval(int argc, char **argv)
{
  if (argc == 0)
    return 1;

  if (strcmp(argv[0], "!") == 0) {
    int r = test_eval(argc - 1, argv + 1);
    return r == 2 ? 2 : !r;
  }

  if (argc == 1)
    return argv[0][0] ? 0 : 1;

  if (argc == 2) {
    if (strcmp(argv[0], "-z") == 0)
      return argv[1][0] ? 1 : 0;
    if (strcmp(argv[0], "-n") == 0)
      return argv[1][0] ? 0 : 1;
    if (strcmp(argv[0], "-e") == 0) {
      int fd = open(argv[1], O_RDONLY);
      if (fd < 0)
        return 1;
      close(fd);
      return 0;
    }
    return 2;
  }

  if (argc == 3) {
    const char *a = argv[0], *op = argv[1], *b = argv[2];
    long x, y;

    if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0)
      return strcmp(a, b) == 0 ? 0 : 1;
    if (strcmp(op, "!=") == 0)
      return strcmp(a, b) != 0 ? 0 : 1;

    x = strtol(a, NULL, 10);
    y = strtol(b, NULL, 10);
    if (strcmp(op, "-eq") == 0) return x == y ? 0 : 1;
    if (strcmp(op, "-ne") == 0) return x != y ? 0 : 1;
    if (strcmp(op, "-lt") == 0) return x <  y ? 0 : 1;
    if (strcmp(op, "-le") == 0) return x <= y ? 0 : 1;
    if (strcmp(op, "-gt") == 0) return x >  y ? 0 : 1;
    if (strcmp(op, "-ge") == 0) return x >= y ? 0 : 1;
  }
  return 2;
}

static int builtin_test(int argc, char **argv)
{
  int n = argc - 1;
  char **a = argv + 1;

  if (argv[0][0] == '[') {
    if (n < 1 || strcmp(a[n - 1], "]") != 0) {
      err3("[: missing ']'", NULL, NULL);
      return 2;
    }
    n--;
  }
  return test_eval(n, a);
}

/* ---- type / help (need the table, so declared first) ---- */

static int builtin_type(int argc, char **argv);
static int builtin_help(int argc, char **argv);

struct builtin {
  const char *name;
  int (*fn)(int argc, char **argv);
};

static const struct builtin builtins[] = {
  { ":",      builtin_true   },
  { "true",   builtin_true   },
  { "false",  builtin_false  },
  { "cd",     builtin_cd     },
  { "pwd",    builtin_pwd    },
  { "echo",   builtin_echo   },
  { "exit",   builtin_exit   },
  { "export", builtin_export },
  { "unset",  builtin_unset  },
  { "set",    builtin_set    },
  { "read",   builtin_read   },
  { "source", builtin_source },
  { ".",      builtin_source },
  { "type",   builtin_type   },
  { "help",   builtin_help   },
  { "test",   builtin_test   },
  { "[",      builtin_test   },
};

#define NUM_BUILTINS (sizeof(builtins) / sizeof(builtins[0]))

static const struct builtin *find_builtin(const char *name)
{
  for (size_t i = 0; i < NUM_BUILTINS; i++)
    if (strcmp(builtins[i].name, name) == 0)
      return &builtins[i];
  return NULL;
}

static int builtin_type(int argc, char **argv)
{
  int status = 0;

  for (int i = 1; i < argc; i++) {
    if (find_builtin(argv[i])) {
      out_puts(argv[i]);
      out_puts(" is a shell builtin\n");
    } else {
      err3("type: ", argv[i], ": not a shell builtin");
      status = 1;
    }
  }
  return status;
}

static int builtin_help(int argc, char **argv)
{
  (void)argc; (void)argv;
  out_puts("Builtins:");
  for (size_t i = 0; i < NUM_BUILTINS; i++) {
    out_puts(" ");
    out_puts(builtins[i].name);
  }
  out_puts("\n");
  return 0;
}

/* ------------------------------------------------------------------ */
/* Execution                                                           */
/* ------------------------------------------------------------------ */

struct command {
  char *argv[MAX_ARGS + 1];     /* NULL terminated */
  int argc;
  const char *in_path;          /* < file   (or NULL) */
  const char *out_path;         /* > or >>  (or NULL) */
  int append;                   /* 1 for >> */
};

/*
 * Builds a NULL-terminated "NAME=VALUE" array from the exported variables,
 * ready to pass to execve(). Free it with free_envp().
 */
__attribute__((unused))
static char **build_envp(void)
{
  int count = 0, k = 0;
  char **envp;

  for (int i = 0; i < MAX_VARS; i++)
    if (vars[i].used && vars[i].exported)
      count++;

  envp = malloc((size_t)(count + 1) * sizeof(char *));
  if (!envp)
    return NULL;

  for (int i = 0; i < MAX_VARS; i++) {
    if (vars[i].used && vars[i].exported) {
      size_t nl = strlen(vars[i].name), vl = strlen(vars[i].value);
      char *s = malloc(nl + 1 + vl + 1);
      if (!s)
        break;
      memcpy(s, vars[i].name, nl);
      s[nl] = '=';
      memcpy(s + nl + 1, vars[i].value, vl + 1);
      envp[k++] = s;
    }
  }
  envp[k] = NULL;
  return envp;
}

__attribute__((unused))
static void free_envp(char **envp)
{
  if (!envp)
    return;
  for (char **e = envp; *e; e++)
    free(*e);
  free(envp);
}

/*
 * ======================= YOUR PART GOES HERE ========================
 * Run an external program.
 *
 *   cmd->argv[0]        program name or path (argv is NULL terminated)
 *   cmd->in_path        file to use as stdin, or NULL
 *   cmd->out_path       file to use as stdout, or NULL (cmd->append for >>)
 *   build_envp()        gives you an environment array for execve()
 *   get_var("PATH")     for searching the program
 *
 * Return the program's exit status (it becomes $?), or 127 if the
 * command was not found.
 *
 * Rough shape with fork+exec:
 *     pid = fork();
 *     if (pid == 0) { (set up redirections) execve(path, argv, envp); _exit(127); }
 *     waitpid(pid, &status, 0);
 * Rough shape with a spawn syscall:
 *     pid = spawn(path, argv, envp); waitpid(pid, &status, 0);
 * =====================================================================
 */
static int external_command(struct command *cmd)
{
  err3(cmd->argv[0], ": running external programs is not implemented yet", NULL);
  return 127;
}

static int run_command(struct command *cmd)
{
  const struct builtin *b;
  int saved_out = out_fd, saved_in = in_fd;
  int opened_in = -1, opened_out = -1;
  int status;

  /* NAME=value with no command: plain variable assignment. */
  if (cmd->argc == 1) {
    char *eq = strchr(cmd->argv[0], '=');
    if (eq && eq != cmd->argv[0]) {
      char name[VAR_NAME_MAX];
      size_t nl = (size_t)(eq - cmd->argv[0]);
      if (nl < VAR_NAME_MAX) {
        memcpy(name, cmd->argv[0], nl);
        name[nl] = '\0';
        if (is_valid_name(name)) {
          if (set_var(name, eq + 1, 0) < 0) {
            err3("cannot set variable ", name, NULL);
            return 1;
          }
          return 0;
        }
      }
    }
  }

  b = find_builtin(cmd->argv[0]);
  if (!b)
    return external_command(cmd);

  if (cmd->in_path) {
    opened_in = open(cmd->in_path, O_RDONLY);
    if (opened_in < 0) {
      err3(cmd->in_path, ": cannot open for reading", NULL);
      return 1;
    }
    in_fd = opened_in;
  }
  if (cmd->out_path) {
    int flags = O_WRONLY | O_CREAT | (cmd->append ? O_APPEND : O_TRUNC);
    opened_out = open(cmd->out_path, flags, 0644);
    if (opened_out < 0) {
      err3(cmd->out_path, ": cannot open for writing", NULL);
      if (opened_in >= 0)
        close(opened_in);
      in_fd = saved_in;
      return 1;
    }
    out_fd = opened_out;
  }

  status = b->fn(cmd->argc, cmd->argv);

  out_fd = saved_out;
  in_fd = saved_in;
  if (opened_in >= 0)
    close(opened_in);
  if (opened_out >= 0)
    close(opened_out);
  return status;
}

static int syntax_error(const char *msg)
{
  err3("syntax error: ", msg, NULL);
  last_status = 2;
  return 2;
}

static int run_tokens(struct parsed *P)
{
  int i = 0, skip = 0;

  while (i < P->count && !should_exit) {
    struct command cmd;
    enum tok_type op;
    int has_redir = 0;

    memset(&cmd, 0, sizeof(cmd));

    while (i < P->count) {
      struct token *t = &P->toks[i];

      if (t->type == T_WORD) {
        if (cmd.argc >= MAX_ARGS) {
          err3("too many arguments", NULL, NULL);
          last_status = 2;
          return 2;
        }
        cmd.argv[cmd.argc++] = (char *)t->text;
        i++;
      } else if (t->type == T_IN || t->type == T_OUT || t->type == T_APPEND) {
        if (i + 1 >= P->count || P->toks[i + 1].type != T_WORD)
          return syntax_error("missing file name after redirection");
        if (t->type == T_IN) {
          cmd.in_path = P->toks[i + 1].text;
        } else {
          cmd.out_path = P->toks[i + 1].text;
          cmd.append = (t->type == T_APPEND);
        }
        has_redir = 1;
        i += 2;
      } else {
        break;
      }
    }

    op = i < P->count ? P->toks[i].type : T_SEMI;
    if (i < P->count)
      i++;

    if (cmd.argc == 0) {
      (void)has_redir;
      if (op == T_AND || op == T_OR)
        return syntax_error("unexpected && or ||");
      skip = 0;
      continue;
    }
    if ((op == T_AND || op == T_OR) && i >= P->count)
      return syntax_error("unexpected end of line after && or ||");

    cmd.argv[cmd.argc] = NULL;

    if (!skip)
      last_status = run_command(&cmd);

    skip = (op == T_AND && last_status != 0) ||
           (op == T_OR && last_status == 0);
  }

  return last_status;
}

static int run_line(const char *line)
{
  struct parsed *P = malloc(sizeof(*P));

  if (!P) {
    err3("out of memory", NULL, NULL);
    last_status = 1;
    return 1;
  }

  if (tokenize(line, P) < 0) {
    last_status = 2;
  } else if (P->count > 0) {
    run_tokens(P);
  }

  free(P);
  return last_status;
}

static int run_script(const char *path)
{
  size_t cap = 1024, len = 0;
  char *buf, *line;
  int fd;

  if (source_depth >= MAX_SOURCE_DEPTH) {
    err3("source: nested too deeply", NULL, NULL);
    return 1;
  }

  fd = open(path, O_RDONLY);
  if (fd < 0) {
    err3(path, ": cannot open file", NULL);
    return 1;
  }

  buf = malloc(cap);
  if (!buf) {
    close(fd);
    err3("out of memory", NULL, NULL);
    return 1;
  }

  for (;;) {
    int n;

    if (len + 1 >= cap) {
      char *bigger;
      if (cap >= MAX_SCRIPT_SIZE) {
        err3(path, ": script too large", NULL);
        break;
      }
      bigger = realloc(buf, cap * 2);
      if (!bigger) {
        err3("out of memory", NULL, NULL);
        break;
      }
      buf = bigger;
      cap *= 2;
    }
    n = read(fd, buf + len, cap - len - 1);
    if (n <= 0)
      break;
    len += (size_t)n;
  }
  close(fd);
  buf[len] = '\0';

  source_depth++;
  line = buf;
  while (*line && !should_exit) {
    char *nl = strchr(line, '\n');
    if (nl)
      *nl = '\0';
    run_line(line);
    line = nl ? nl + 1 : line + strlen(line);
  }
  source_depth--;

  free(buf);
  return last_status;
}

/* ------------------------------------------------------------------ */
/* Interactive loop                                                    */
/* ------------------------------------------------------------------ */

static void print_prompt(void)
{
  char cwd[CWD_MAX];

  if (getcwd(cwd, sizeof(cwd)))
    fd_puts(1, cwd);
  else
    fd_puts(1, "?");
  fd_puts(1, "$ ");
}

/* Returns line length, or -1 on end of input. */
static int read_line(char *buf, size_t size)
{
  size_t len = 0;
  int too_long = 0;
  char c;

  for (;;) {
    int n = read(0, &c, 1);

    if (n <= 0) {
      if (len == 0 && !too_long)
        return -1;
      break;
    }
    if (c == '\n')
      break;
    if (c == '\r')
      continue;
    if (len + 1 < size)
      buf[len++] = c;
    else
      too_long = 1;
  }

  buf[len] = '\0';
  if (too_long) {
    err3("line too long", NULL, NULL);
    buf[0] = '\0';
    return 0;
  }
  return (int)len;
}

static void init_shell(void)
{
  char cwd[CWD_MAX];

  set_var("PATH", "/bin", 1);
  set_var("HOME", "/", 1);
  if (getcwd(cwd, sizeof(cwd)))
    set_var("PWD", cwd, 1);
}

int main(int argc, char **argv)
{
  char line[MAX_LINE];

  init_shell();

  /* `shell script.sh` runs a script and exits. */
  if (argc > 1) {
    run_script(argv[1]);
    return last_status;
  }

  fd_puts(1, "Type 'help' to list builtins.\n");

  while (!should_exit) {
    print_prompt();
    if (read_line(line, sizeof(line)) < 0) {
      fd_puts(1, "exit\n");
      break;
    }
    run_line(line);
  }

  return last_status;
}
