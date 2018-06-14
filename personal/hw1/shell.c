#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "tokenizer.h"

/* Convenience macro to silence compiler warnings about unused function parameters. */
#define unused __attribute__((unused))

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

void write_to_file(int pipe, char *filename) {
  FILE *fd_in, *fd_out;
  fd_out = fopen(filename, "w");
  if (!fd_out) {
    fprintf(stdout, "Error opening file: %s. Error: %s\n", filename, strerror(errno));
    return;
  }

  fd_in = fdopen(pipe, "r");
  if (!fd_in) {
    fprintf(stdout, "Error opening pipe. Error: %s\n", strerror(errno));
    return;
  }

  int c;
  while ((c = fgetc(fd_in)) != EOF) {
    fputc(c, fd_out);
  }

  fclose(fd_out);
  fclose(fd_in);
}

void read_from_file(int pipe, char *filename) {
  FILE *fd_in, *fd_out;
  fd_in = fopen(filename, "r");
  if (!fd_in) {
    fprintf(stdout, "Error opening file: %s. Error: %s\n", filename, strerror(errno));
    return;
  }

  fd_out = fdopen(pipe, "w");
  if (!fd_out) {
    fprintf(stdout, "Error opening pipe. Error: %s\n", strerror(errno));
    return;
  }

  int c;
  while ((c = fgetc(fd_in)) != EOF) {
    fputc(c, fd_out);
  }

  fclose(fd_out);
  fclose(fd_in);
}

int cmd_exit(struct tokens *tokens);
int cmd_help(struct tokens *tokens);
int cmd_pwd(struct tokens *tokens);
int cmd_cd(struct tokens *tokens);

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens *tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t *fun;
  char *cmd;
  char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
  {cmd_pwd, "pwd", "show current working directory"},
  {cmd_cd, "cd", "enter directory"},
  {cmd_help, "?", "show this help menu"},
  {cmd_exit, "exit", "exit the command shell"},
};

/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens *tokens) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

/* Exits this shell */
int cmd_exit(unused struct tokens *tokens) {
  exit(0);
}

/* Prints current directory */
int cmd_pwd(unused struct tokens *tokens) {
  char cwd[1024];
  if (getcwd(cwd, sizeof(cwd)) != NULL) {
    printf("Current working dir: %s\n", cwd);
    return 0;
  } else {
    perror("getcwd() error");
    return 1;
  }
}

/* Enters specified directory */
int cmd_cd(struct tokens *tokens) {

  /* get path from first argument */
  char *path = tokens_get_token(tokens, 1);

  if (chdir(path) == 0) {
    printf("Entering: %s\n", path);
    return 0;
  } else {
    perror("chdir() error");
    return 1;
  }

}

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
      return i;
  return -1;
}

/* Executes non-built in command */
int execute(struct tokens *tokens) {
  char *args[1024];
  char *env_paths;
  char *output_arg;
  char *input_arg;
  int has_output = 0;
  int has_input = 0;
  int pipefd[2];
  pid_t cpid;
  pid_t cpid2;
  int exec_result;
  int child_status = 0;

  /* get path from first argument */
  char *path_arg = tokens_get_token(tokens, 0);
  size_t num_tokens = tokens_get_length(tokens);

  size_t i = 0;
  for (i = 0; i < num_tokens; i++) {
    char *arg = tokens_get_token(tokens, i);
    if (strncmp(arg, ">", 1) == 0) {
      output_arg = tokens_get_token(tokens, i+1);
      has_output = 1;
      break;
    } else if (strncmp(arg, "<", 1) == 0) {
      input_arg = tokens_get_token(tokens, i+1);
      has_input = 1;
      break;
    } else {
      args[i] = arg;
    }
  }
  args[i] = NULL;

  if (pipe(pipefd) == -1) {
    fprintf(stdout, "Error creating pipe. Error: %s\n", strerror(errno));
  }

  cpid = fork();

  if (cpid > 0) {
    /* parent process */
    close(pipefd[1]);

    if (has_output) {
      write_to_file(pipefd[0], output_arg);
    }

    close(pipefd[0]);

    wait(&child_status);
  } else if (cpid == 0) {
    /* child process */

    if (has_output) {
      if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
        fprintf(stdout, "Error duplicating pipe. Error: %s\n", strerror(errno));
        return 1;
      }
    }

    if (has_input) {
      if (dup2(pipefd[0], STDIN_FILENO) == -1) {
        fprintf(stdout, "Error duplicating pipe. Error: %s\n", strerror(errno));
        return 1;
      }

      cpid2 = fork();
      if (cpid2 == 0) {
        /* child */
        read_from_file(pipefd[1], input_arg);
        close(pipefd[1]);
        close(pipefd[0]);
        exit(0);
      } else if (cpid2 > 0) {
        /* parent */
        waitpid(cpid2, &child_status, 0);
      } else {
        /* error */
        fprintf(stdout, "Error executing fork. Error: %s\n", strerror(errno));
      }
    }

    close(pipefd[1]);
    close(pipefd[0]);

    env_paths = getenv ("PATH");
    if (env_paths != NULL) {
      char *env_path = strtok(env_paths, ":");
      while(env_path) {
        char full_path[1024];
        strncpy(full_path, env_path, 1024);
        strcat(full_path, "/");
        strncat(full_path, path_arg, 1024 - strlen(full_path));
        exec_result = execv(full_path, args);
        env_path = strtok(NULL, ":");
      }
    }

    exec_result = execv(path_arg, args);
    if (exec_result == -1) {
      fprintf(stdout, "Error running %s. Error: %s\n", path_arg, strerror(errno));
    }

    exit(1);
  } else {
    /* error */
    fprintf(stdout, "Error running %s. Error: %s\n", path_arg, strerror(errno));
  }

  return 0;
}

/* Intialization procedures for this shell */
void init_shell() {
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive) {
    /* If the shell is not currently in the foreground, we must pause the shell until it becomes a
     * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the
     * foreground, we'll receive a SIGCONT. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save the current termios to a variable, so it can be restored later. */
    tcgetattr(shell_terminal, &shell_tmodes);
  }
}

int main(unused int argc, unused char *argv[]) {
  init_shell();

  static char line[4096];
  int line_num = 0;

  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", line_num);

  while (fgets(line, 4096, stdin)) {
    /* Split our line into words. */
    struct tokens *tokens = tokenize(line);

    /* Find which built-in function to run. */
    int fundex = lookup(tokens_get_token(tokens, 0));

    if (fundex >= 0) {
      cmd_table[fundex].fun(tokens);
    } else {
      /* REPLACE this to run commands as programs. */
      fprintf(stdout, "Attempting to run: %s\n", tokens_get_token(tokens, 0));
      execute(tokens);
    }

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    tokens_destroy(tokens);
  }

  return 0;
}
