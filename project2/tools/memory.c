#include "tools.h"

#include "config.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char *memory_dir_path(void) {
  return xasprintf("%s/.agent", g_config.workdir);
}

static char *memory_file_path(void) {
  return xasprintf("%s/.agent/memory.md", g_config.workdir);
}

static int ensure_memory_dir(void) {
  char *dir = memory_dir_path();
  int rc = mkdir(dir, 0755);
  if (rc != 0 && errno == EEXIST)
    rc = 0;
  free(dir);
  return rc;
}

static ToolResult tool_memory_read(cJSON *args) {
  (void)args;

  char *path = memory_file_path();
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    if (errno == ENOENT) {
      free(path);
      return (ToolResult){
          .ok = true,
          .output = xstrdup("Project memory is empty."),
      };
    }

    char *msg = xasprintf("memory_read: cannot open .agent/memory.md: %s",
                          strerror(errno));
    free(path);
    return (ToolResult){.ok = false, .output = msg};
  }

  if (fseek(fp, 0, SEEK_END) != 0) {
    char *msg = xasprintf("memory_read: cannot seek .agent/memory.md: %s",
                          strerror(errno));
    fclose(fp);
    free(path);
    return (ToolResult){.ok = false, .output = msg};
  }

  long file_size = ftell(fp);
  if (file_size < 0) {
    char *msg = xasprintf("memory_read: cannot tell .agent/memory.md size: %s",
                          strerror(errno));
    fclose(fp);
    free(path);
    return (ToolResult){.ok = false, .output = msg};
  }
  rewind(fp);

  char *content = xmalloc((size_t)file_size + 1);
  size_t nread = fread(content, 1, (size_t)file_size, fp);
  if (nread != (size_t)file_size && ferror(fp)) {
    char *msg = xasprintf("memory_read: cannot read .agent/memory.md: %s",
                          strerror(errno));
    free(content);
    fclose(fp);
    free(path);
    return (ToolResult){.ok = false, .output = msg};
  }
  content[nread] = '\0';

  fclose(fp);
  free(path);
  if (nread == 0) {
    free(content);
    content = xstrdup("Project memory is empty.");
  }
  return (ToolResult){.ok = true, .output = content};
}

static ToolResult tool_memory_write(cJSON *args) {
  const char *content =
      cJSON_GetStringValue(cJSON_GetObjectItem(args, "content"));
  const char *mode = cJSON_GetStringValue(cJSON_GetObjectItem(args, "mode"));

  if (!content) {
    return (ToolResult){
        .ok = false,
        .output = xstrdup("memory_write: missing 'content' argument"),
    };
  }

  bool replace = mode && strcmp(mode, "replace") == 0;
  if (mode && !replace && strcmp(mode, "append") != 0) {
    return (ToolResult){
        .ok = false,
        .output = xstrdup("memory_write: mode must be 'append' or 'replace'"),
    };
  }

  if (ensure_memory_dir() != 0) {
    return (ToolResult){
        .ok = false,
        .output = xasprintf("memory_write: cannot create .agent: %s",
                            strerror(errno)),
    };
  }

  char *path = memory_file_path();
  FILE *fp = fopen(path, replace ? "wb" : "ab");
  if (!fp) {
    char *msg = xasprintf("memory_write: cannot open .agent/memory.md: %s",
                          strerror(errno));
    free(path);
    return (ToolResult){.ok = false, .output = msg};
  }

  size_t len = strlen(content);
  size_t written = fwrite(content, 1, len, fp);
  if (written == len && (len == 0 || content[len - 1] != '\n'))
    written += fwrite("\n", 1, 1, fp);

  if (fclose(fp) != 0) {
    char *msg = xasprintf("memory_write: failed to close .agent/memory.md: %s",
                          strerror(errno));
    free(path);
    return (ToolResult){.ok = false, .output = msg};
  }

  free(path);
  if (written < len) {
    return (ToolResult){
        .ok = false,
        .output = xstrdup("memory_write: short write to .agent/memory.md"),
    };
  }

  return (ToolResult){
      .ok = true,
      .output = xasprintf("%s project memory (%zu bytes)",
                          replace ? "replaced" : "updated", len),
  };
}

ToolDef memory_read_def = {
    .name = "memory_read",
    .desc = "Read persistent project memory from .agent/memory.md.",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{},"
                    "\"required\":[]}",
    .exec = tool_memory_read,
    .read_only = true,
};

ToolDef memory_write_def = {
    .name = "memory_write",
    .desc = "Append to or replace persistent project memory in .agent/memory.md.",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{"
                    "\"content\":{\"type\":\"string\","
                    "\"description\":\"Memory text to store\"},"
                    "\"mode\":{\"type\":\"string\","
                    "\"enum\":[\"append\",\"replace\"],"
                    "\"description\":\"append by default; replace rewrites the memory file\"}"
                    "},"
                    "\"required\":[\"content\"]}",
    .exec = tool_memory_write,
    .read_only = false,
};
