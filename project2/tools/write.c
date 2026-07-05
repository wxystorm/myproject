#include "tools.h"
#include "sandbox.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ToolResult tool_write_file(cJSON *args) {
  const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(args, "path"));
  const char *content = cJSON_GetStringValue(cJSON_GetObjectItem(args, "content"));

  if (!path) {
    return (ToolResult){
        .ok = false,
        .output = xstrdup("write_file: missing 'path' argument"),
    };
  }

  if (!content) {
    return (ToolResult){
        .ok = false,
        .output = xstrdup("write_file: missing 'content' argument"),
    };
  }

  char *abs_path = resolve_workspace_path(path);
  if (!abs_path) {
    return (ToolResult){
        .ok = false,
        .output = xasprintf("write_file: path is outside workspace or parent does not exist: %s", path),
    };
  }

  FILE *fp = fopen(abs_path, "wb");
  if (!fp) {
    char *msg = xasprintf("write_file: cannot open %s: %s",
                          path, strerror(errno));
    free(abs_path);
    return (ToolResult){
        .ok = false,
        .output = msg,
    };
  }

  size_t len = strlen(content);
  size_t written = fwrite(content, 1, len, fp);

  if (fclose(fp) != 0) {
    char *msg = xasprintf("write_file: failed to close %s: %s",
                          path, strerror(errno));
    free(abs_path);
    return (ToolResult){
        .ok = false,
        .output = msg,
    };
  }

  free(abs_path);

  if (written != len) {
    return (ToolResult){
        .ok = false,
        .output = xasprintf("write_file: short write to %s: wrote %zu of %zu bytes",
                            path, written, len),
    };
  }

  return (ToolResult){
      .ok = true,
      .output = xasprintf("wrote %zu bytes to %s", written, path),
  };
}

ToolDef write_file_def = {
    .name = "write_file",
    .desc = "Write full contents to a file inside the workspace.",
    .param_schema =  "{\"type\":\"object\","
    "\"properties\":{"
      "\"path\":{\"type\":\"string\","
      "\"description\":\"Relative path inside the workspace\"},"
      "\"content\":{\"type\":\"string\","
      "\"description\":\"Full file contents to write\"}"
    "},"
    "\"required\":[\"path\",\"content\"]}",
    .exec = tool_write_file,
    .read_only = false,
};