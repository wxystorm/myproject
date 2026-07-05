#include "tools.h"
#include "sandbox.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *READ_FILE_SCHEMA =
    "{\"type\":\"object\","
    "\"properties\":{"
      "\"path\":{\"type\":\"string\","
      "\"description\":\"Relative path inside the workspace\"},"
      "\"limit\":{\"type\":\"integer\","
      "\"description\":\"Optional maximum number of lines to return\"}"
    "},"
    "\"required\":[\"path\"]}";

static ToolResult tool_read_file(cJSON *args) {
  const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(args, "path"));
  if (!path) {
    return (ToolResult){
        .ok = false,
        .output = xstrdup("read_file: missing 'path' argument"),
    };
  }

  char *abs_path = resolve_workspace_path(path);
  if (!abs_path) {
    return (ToolResult){
        .ok = false,
        .output = xasprintf("read_file: path is outside workspace: %s", path),
    };
  }

  FILE *fp = fopen(abs_path, "rb");
  if (!fp) {
    char *msg = xasprintf("read_file: cannot open %s: %s",
                          path, strerror(errno));
    free(abs_path);
    return (ToolResult){
        .ok = false,
        .output = msg,
    };
  }

  /*
   * 后面再读取文件内容。
   */
  if (fseek(fp, 0, SEEK_END) != 0) {
    char *msg = xasprintf("read_file: cannot seek %s: %s", path,
                          strerror(errno));
    fclose(fp);
    free(abs_path);
    return (ToolResult){
        .ok = false,
        .output = msg,
    };
  }

  long file_size = ftell(fp);
  if (file_size < 0) {
    char *msg = xasprintf("read_file: cannot tell size of %s: %s", path,
                          strerror(errno));
    fclose(fp);
    free(abs_path);
    return (ToolResult){
        .ok = false,
        .output = msg,
    };
  }

  if (fseek(fp, 0, SEEK_SET) != 0) {
    char *msg = xasprintf("read_file: cannot rewind %s: %s", path,
                          strerror(errno));
    fclose(fp);
    free(abs_path);
    return (ToolResult){
        .ok = false,
        .output = msg,
    };
  }

  char *content = xmalloc((size_t)file_size + 1);
  size_t bytes_read = fread(content, 1, (size_t)file_size, fp);
  if (bytes_read != (size_t)file_size && ferror(fp)) {
    char *msg = xasprintf("read_file: cannot read %s: %s", path,
                          strerror(errno));
    free(content);
    fclose(fp);
    free(abs_path);
    return (ToolResult){
        .ok = false,
        .output = msg,
    };
  }

  content[bytes_read] = '\0';

  fclose(fp);
  free(abs_path);

  return (ToolResult){
      .ok = true,
      .output = content,
  };
  
}

ToolDef read_file_def = {
    .name = "read_file",
    .desc = "Read a UTF-8 text file inside the workspace.",
    .param_schema = "{\"type\":\"object\","
    "\"properties\":{"
      "\"path\":{\"type\":\"string\","
      "\"description\":\"Relative path inside the workspace\"},"
      "\"limit\":{\"type\":\"integer\","
      "\"description\":\"Optional maximum number of lines to return\"}"
    "},"
    "\"required\":[\"path\"]}",
    .exec = tool_read_file,
    .read_only = true,
};