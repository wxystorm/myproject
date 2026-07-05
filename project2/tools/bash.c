/*
 * bash tool — run a shell command in a forked child and return its output
 * as a ToolResult.
 *
 * The child side (pipe setup, fork, dup2, execl) is given below — you have
 * already written this pattern in Project 1. What the LLM actually needs is
 * the *other half*: after fork, the parent holds pipefd[0] and pid, and
 * must turn that into a ToolResult the agent can send back.
 */
#include "tools.h"
#include "config.h"
#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>


const char *BASH_TOOL_NAME = "bash";
const char *BASH_TOOL_DESC =
    "Run a shell command and return its combined stdout/stderr.";
const char *BASH_TOOL_SCHEMA =
    "{\"type\":\"object\","
    "\"properties\":{\"command\":{\"type\":\"string\","
    "\"description\":\"The shell command to execute\"}},"
    "\"required\":[\"command\"]}";

static int path_under_workspace(const char *path) {
  size_t n = strlen(g_config.workdir);
  return strncmp(path, g_config.workdir, n) == 0 &&
         (path[n] == '/' || path[n] == '\0');
}

static int has_parent_component(const char *s) {
  return strcmp(s, "..") == 0 || strncmp(s, "../", 3) == 0 ||
         strstr(s, "/../") != NULL ||
         (strlen(s) >= 3 && strcmp(s + strlen(s) - 3, "/..") == 0);
}

static int is_shell_separator(unsigned char ch) {
  return isspace(ch) || ch == '<' || ch == '>' || ch == '|' || ch == ';' ||
         ch == '&' || ch == '(' || ch == ')';
}

static const char *unsafe_path_in_token(const char *token) {
  if (!token || token[0] == '\0')
    return NULL;

  if (token[0] == '~' && (token[1] == '/' || token[1] == '\0'))
    return token;

  if (has_parent_component(token))
    return token;

  if (token[0] == '/' && !path_under_workspace(token))
    return token;

  const char *eq = strchr(token, '=');
  if (eq && eq[1] == '/' && !path_under_workspace(eq + 1))
    return eq + 1;

  return NULL;
}

static char *bash_sandbox_violation(const char *cmd) {
  char token[1024];
  size_t len = 0;
  char quote = '\0';

  for (const char *p = cmd; ; p++) {
    unsigned char ch = (unsigned char)*p;
    int at_end = ch == '\0';

    if (!at_end && quote) {
      if (ch == quote)
        quote = '\0';
      else if (len + 1 < sizeof(token))
        token[len++] = (char)ch;
      continue;
    }

    if (!at_end && (ch == '\'' || ch == '"')) {
      quote = (char)ch;
      continue;
    }

    if (at_end || is_shell_separator(ch)) {
      if (len > 0) {
        token[len] = '\0';
        const char *bad = unsafe_path_in_token(token);
        if (bad) {
          return xasprintf(
              "bash: sandbox blocked path outside workspace: %s\n"
              "Ask the user for approval or use a path under %s.",
              bad, g_config.workdir);
        }
        len = 0;
      }
      if (at_end)
        break;
      continue;
    }

    if (len + 1 < sizeof(token))
      token[len++] = (char)ch;
  }

  return NULL;
}

/*void tool_result_free(ToolResult *r) {
  if (!r)
    return;
  free(r->output);
  r->output = NULL;
}
*/

ToolResult bash_tool_exec(cJSON *args) {
  const char *cmd = cJSON_GetStringValue(cJSON_GetObjectItem(args, "command"));
  if (!cmd)
    return (ToolResult){.ok = false,
                        .output = xstrdup("missing 'command' argument")};

  char *sandbox_error = bash_sandbox_violation(cmd);
  if (sandbox_error) {
    return (ToolResult){
        .ok = false,
        .output = sandbox_error,
    };
  }

  int pipefd[2];
  if (pipe(pipefd) != 0)
    return (ToolResult){
        .ok = false,
        .output = xasprintf("pipe failed: %s", strerror(errno)),
    };

  pid_t pid = fork();
  if (pid < 0) {
    int e = errno;
    close(pipefd[0]);
    close(pipefd[1]);
    return (ToolResult){.ok = false,
                        .output = xasprintf("fork: %s", strerror(e))};
  }

  if (pid == 0) {   //子进程
    close(pipefd[0]);
    /* dprintf + _exit avoid stdio-buffer double-flush after fork. */
    if (dup2(pipefd[1], STDOUT_FILENO) < 0 ||
        dup2(pipefd[1], STDERR_FILENO) < 0)
      _exit(127);
    close(pipefd[1]);
    execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
    dprintf(STDERR_FILENO, "exec failed: %s\n", strerror(errno));
    _exit(127);
  }

  close(pipefd[1]);

  /*
   * TODO(student, Part 1A):
   *
   * You are in the parent. The child is running `cmd` with its stdout and
   * stderr both wired to pipefd[1]; you hold pipefd[0] and pid.
   *
   * Produce a ToolResult describing what the child did. The LLM will read
   * whatever you put in .output verbatim on the next turn, so think about
   * what it would actually want to see — *including* when the command
   * failed. A non-zero exit or a fatal signal is information the LLM needs
   * to react to; see waitpid(2) and the WIFEXITED / WEXITSTATUS /
   * WIFSIGNALED / WTERMSIG macros in <sys/wait.h>.
   *
   * Remember to close pipefd[0] and reap the child before returning.
   */
  
  char buffer[1024];
  ssize_t bytes_read;
  char *output = NULL;
  size_t total_bytes = 0;

  while ((bytes_read = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
    char *tmp = realloc(output, total_bytes + (size_t)bytes_read + 1);
    if (!tmp) {
      free(output);
      close(pipefd[0]);

      int status;
      waitpid(pid, &status, 0);

      return (ToolResult){
          .ok = false,
          .output = xstrdup("memory allocation failed"),
      };
    }

    output = tmp;
    memcpy(output + total_bytes, buffer, (size_t)bytes_read);
    total_bytes += (size_t)bytes_read;
  }

  if (bytes_read < 0) {
    int e = errno;
    free(output);
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    return (ToolResult){
        .ok = false,
        .output = xasprintf("read failed: %s", strerror(e)),
    };
  }

  close(pipefd[0]);

  if (!output) {
    output = xstrdup("");
  } else {
    output[total_bytes] = '\0';
  }

  int status;
  if (waitpid(pid, &status, 0) < 0) {
    int e = errno;
    char *msg = xasprintf("%s\n[waitpid failed: %s]\n", output, strerror(e));
    free(output);
    return (ToolResult){
        .ok = false,
        .output = msg,
    };
  }

  if (WIFEXITED(status)) {
    int code = WEXITSTATUS(status);

    if (code == 0) {
      return (ToolResult){
          .ok = true,
          .output = output,
      };
    } else {
      char *msg = xasprintf("%s\n[Process exited with status %d]\n",
                            output, code);
      free(output);
      return (ToolResult){
          .ok = false,
          .output = msg,
      };
    }
  }

  if (WIFSIGNALED(status)) {
    int sig = WTERMSIG(status);
    char *msg = xasprintf("%s\n[Process terminated by signal %d]\n",
                          output, sig);
    free(output);
    return (ToolResult){
        .ok = false,
        .output = msg,
    };
  }

  char *msg = xasprintf("%s\n[Process ended unexpectedly]\n", output);
  free(output);
  return (ToolResult){
      .ok = false,
      .output = msg,
  };
}
ToolDef bash_def = {
    .name = "bash",
    .desc = "Run a shell command and return its combined stdout/stderr.",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{\"command\":{\"type\":\"string\","
                    "\"description\":\"The shell command to execute\"}},"
                    "\"required\":[\"command\"]}",
    .exec = bash_tool_exec,
    .read_only = false,
};
