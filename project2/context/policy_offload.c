#include "context/internal.h"

#include "config.h"
#include "util.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define OFFLOAD_MIN_CHARS 512
#define OFFLOAD_PREVIEW_CHARS 120

static void set_err(char *err, size_t err_cap, const char *fmt, ...) {
  if (!err || err_cap == 0)
    return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(err, err_cap, fmt, ap);
  va_end(ap);
}

static int mkdir_p(const char *path) {
  char buf[PATH_MAX];
  snprintf(buf, sizeof(buf), "%s", path);
  size_t len = strlen(buf);
  if (len == 0)
    return 0;

  for (char *p = buf + 1; *p; p++) {
    if (*p != '/')
      continue;
    *p = '\0';
    if (mkdir(buf, 0755) != 0 && errno != EEXIST)
      return -1;
    *p = '/';
  }

  if (mkdir(buf, 0755) != 0 && errno != EEXIST)
    return -1;
  return 0;
}

static int write_text_file(const char *path, const char *content) {
  FILE *fp = fopen(path, "wb");
  if (!fp)
    return -1;

  size_t len = strlen(content);
  size_t written = fwrite(content, 1, len, fp);
  int rc = 0;
  if (written != len || fclose(fp) != 0)
    rc = -1;
  return rc;
}

static char *make_preview_message(cJSON *original, const char *preview) {
  cJSON *obj = cJSON_CreateObject();
  if (!obj)
    return NULL;

  cJSON_AddStringToObject(obj, "role", "tool");
  const char *tool_call_id = json_str(original, "tool_call_id");
  if (tool_call_id)
    cJSON_AddStringToObject(obj, "tool_call_id", tool_call_id);
  cJSON_AddStringToObject(obj, "content", preview);

  char *json = cJSON_PrintUnformatted(obj);
  cJSON_Delete(obj);
  return json;
}

static bool offload_should_apply(Context *ctx) {
  return ctx_budget_usage(ctx) > g_config.offload_threshold;
}

static int offload_apply(Context *ctx, char *err, size_t err_cap) {
  char *offload_dir = xasprintf("%s/.agent/offload", g_config.workdir);
  if (mkdir_p(offload_dir) != 0) {
    set_err(err, err_cap, "failed to create offload directory: %s",
            offload_dir);
    free(offload_dir);
    return -1;
  }

  int limit = ctx->history.len - KEEP_RECENT_MSGS;
  if (limit <= 0)
    return 0;

  for (int i = 0; i < limit; i++) {
    cJSON *msg = cJSON_Parse(ctx->history.items[i]);
    if (!msg)
      continue;

    const char *role = json_str(msg, "role");
    const char *content = json_str(msg, "content");
    if (!role || strcmp(role, "tool") != 0 || !content ||
        strlen(content) < OFFLOAD_MIN_CHARS) {
      cJSON_Delete(msg);
      continue;
    }

    int file_id = ctx->next_offload_id++;
    char *file_path = xasprintf("%s/%d.txt", offload_dir, file_id);
    if (write_text_file(file_path, content) != 0) {
      cJSON_Delete(msg);
      set_err(err, err_cap, "failed to write offload file: %s", file_path);
      free(file_path);
      free(offload_dir);
      return -1;
    }

    size_t preview_len = strlen(content);
    if (preview_len > OFFLOAD_PREVIEW_CHARS)
      preview_len = OFFLOAD_PREVIEW_CHARS;
    char *preview = xasprintf(
        "%.*s\n[offloaded: read_file %s/.agent/offload/%d.txt]",
        (int)preview_len, content, g_config.workdir, file_id);
    char *preview_json = make_preview_message(msg, preview);
    free(preview);
    if (!preview_json) {
      cJSON_Delete(msg);
      set_err(err, err_cap, "failed to serialize offload preview");
      free(file_path);
      free(offload_dir);
      return -1;
    }
    ctx_replace_msg(ctx, i, preview_json);
    free(file_path);
    cJSON_Delete(msg);
  }

  free(offload_dir);
  return 0;
}

ContextPolicy offload_policy = {
    .name = "offload",
    .should_apply = offload_should_apply,
    .apply = offload_apply,
};