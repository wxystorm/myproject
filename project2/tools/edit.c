#include "tools.h"
#include "sandbox.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



static ToolResult tool_edit_file(cJSON *args) {
  const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(args, "path"));
  const char *old_text = cJSON_GetStringValue(cJSON_GetObjectItem(args, "old_text"));
  const char *new_text = cJSON_GetStringValue(cJSON_GetObjectItem(args, "new_text"));

  if (!path) {
    return (ToolResult){
        .ok = false,
        .output = xstrdup("edit_file: missing 'path' argument"),
    };
  }

  if (!old_text) {
    return (ToolResult){
        .ok = false,
        .output = xstrdup("edit_file: missing 'old_text' argument"),
    };
  }

  if (!new_text) {
    return (ToolResult){
        .ok = false,
        .output = xstrdup("edit_file: missing 'new_text' argument"),
    };
  }

  if (old_text[0] == '\0') {
    return (ToolResult){
        .ok = false,
        .output = xstrdup("edit_file: old_text must not be empty"),
    };
  }

  char *abs_path = resolve_workspace_path(path);
  if (!abs_path) {
    return (ToolResult){
        .ok = false,
        .output = xasprintf("edit_file: path is outside workspace: %s", path),
    };
  }

  FILE *fp = fopen(abs_path, "rb");
  if (!fp) {
    char *msg = xasprintf("edit_file: cannot open %s: %s",
                          path, strerror(errno));
    free(abs_path);
    return (ToolResult){.ok = false, .output = msg};
  }

  if (fseek(fp, 0, SEEK_END) != 0) {
    char *msg = xasprintf("edit_file: failed to seek %s: %s",
                          path, strerror(errno));
    fclose(fp);
    free(abs_path);
    return (ToolResult){.ok = false, .output = msg};
  }

  long file_size_long = ftell(fp);
  if (file_size_long < 0) {
    char *msg = xasprintf("edit_file: failed to tell size of %s: %s",
                          path, strerror(errno));
    fclose(fp);
    free(abs_path);
    return (ToolResult){.ok = false, .output = msg};
  }

  size_t file_size = (size_t)file_size_long;
  rewind(fp);

  char *content = malloc(file_size + 1);
  if (!content) {
    fclose(fp);
    free(abs_path);
    return (ToolResult){
        .ok = false,
        .output = xstrdup("edit_file: memory allocation failed"),
    };
  }

  size_t nread = fread(content, 1, file_size, fp);
  if (nread != file_size && ferror(fp)) {
    char *msg = xasprintf("edit_file: failed to read %s: %s",
                          path, strerror(errno));
    free(content);
    fclose(fp);
    free(abs_path);
    return (ToolResult){.ok = false, .output = msg};
  }

  content[nread] = '\0';
  fclose(fp);

  char *pos = strstr(content, old_text);
  if (!pos) {
    free(content);
    free(abs_path);
    return (ToolResult){
        .ok = false,
        .output = xasprintf("edit_file: old_text not found in %s", path),
    };
  }

  size_t prefix_len = (size_t)(pos - content);
  size_t old_len = strlen(old_text);
  size_t new_len = strlen(new_text);
  size_t suffix_len = nread - prefix_len - old_len;
  size_t new_size = prefix_len + new_len + suffix_len;

  char *new_content = malloc(new_size + 1);
  if (!new_content) {
    free(content);
    free(abs_path);
    return (ToolResult){
        .ok = false,
        .output = xstrdup("edit_file: memory allocation failed"),
    };
  }

  memcpy(new_content, content, prefix_len);
  memcpy(new_content + prefix_len, new_text, new_len);
  memcpy(new_content + prefix_len + new_len,
         pos + old_len,
         suffix_len);
  new_content[new_size] = '\0';

  free(content);

  fp = fopen(abs_path, "wb");
  if (!fp) {
    char *msg = xasprintf("edit_file: cannot write %s: %s",
                          path, strerror(errno));
    free(new_content);
    free(abs_path);
    return (ToolResult){.ok = false, .output = msg};
  }

  size_t written = fwrite(new_content, 1, new_size, fp);

  if (fclose(fp) != 0) {
    char *msg = xasprintf("edit_file: failed to close %s: %s",
                          path, strerror(errno));
    free(new_content);
    free(abs_path);
    return (ToolResult){.ok = false, .output = msg};
  }

  free(new_content);
  free(abs_path);

  if (written != new_size) {
    return (ToolResult){
        .ok = false,
        .output = xasprintf("edit_file: short write to %s", path),
    };
  }

  return (ToolResult){
      .ok = true,
      .output = xasprintf("edited %s: replaced first occurrence", path),
  };
}

ToolDef edit_file_def = {
    .name = "edit_file",
    .desc = "Replace the first exact occurrence of text in a file inside the workspace.",
    .param_schema = "{\"type\":\"object\","
    "\"properties\":{"
      "\"path\":{\"type\":\"string\","
      "\"description\":\"Relative path inside the workspace\"},"
      "\"old_text\":{\"type\":\"string\","
      "\"description\":\"Exact substring to find\"},"
      "\"new_text\":{\"type\":\"string\","
      "\"description\":\"Replacement text\"}"
    "},"
    "\"required\":[\"path\",\"old_text\",\"new_text\"]}",
    .exec = tool_edit_file,
    .read_only = false,
};