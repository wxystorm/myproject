#include "context/internal.h"

#include "agent/llm_client.h"
#include "config.h"
#include "message.h"
#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_err(char *err, size_t err_cap, const char *fmt, ...) {
  if (!err || err_cap == 0)
    return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(err, err_cap, fmt, ap);
  va_end(ap);
}

static void free_llm_response(LLMResponse *response) {
  if (!response)
    return;
  free(response->content);
  free(response->raw_message);
  if (response->tool_calls) {
    for (int i = 0; i < response->n_tool_calls; i++) {
      free(response->tool_calls[i].id);
      free(response->tool_calls[i].name);
      cJSON_Delete(response->tool_calls[i].args);
    }
    free(response->tool_calls);
  }
  memset(response, 0, sizeof(*response));
}

static char *build_summary_prompt(const Context *ctx, int prefix_end) {
  static const char prefix[] =
      "Summarize the older conversation history into a compact handoff. "
      "Preserve facts, decisions, outcomes, and unresolved tasks.\n\n";
  size_t total = sizeof(prefix);
  for (int i = 0; i < prefix_end; i++)
    total += strlen(ctx->history.items[i]) + 1;

  char *buf = xmalloc(total);
  size_t pos = 0;
  memcpy(buf + pos, prefix, sizeof(prefix) - 1);
  pos += sizeof(prefix) - 1;
  for (int i = 0; i < prefix_end; i++) {
    size_t len = strlen(ctx->history.items[i]);
    memcpy(buf + pos, ctx->history.items[i], len);
    pos += len;
    buf[pos++] = '\n';
  }
  buf[pos] = '\0';
  return buf;
}

static bool summary_should_apply(Context *ctx) {
  return ctx->history.len > KEEP_RECENT_MSGS &&
         ctx_budget_usage(ctx) > g_config.summary_threshold;
}

static int summary_apply(Context *ctx, char *err, size_t err_cap) {
  if (ctx->history.len <= KEEP_RECENT_MSGS)
    return 0;
  fprintf(stderr, "[context] summary triggered\n");
  int prefix_end = ctx->history.len - KEEP_RECENT_MSGS;
  char *prompt = build_summary_prompt(ctx, prefix_end);

  MessageList request;
  msg_list_init(&request);
  msg_list_push(&request, msg_user_json(prompt));
  free(prompt);

  LLMResponse response = {0};
  int rc = llm_chat(&request, "You compress older conversation history into a concise handoff for future turns. Keep key facts, decisions, outcomes, and unresolved items. Do not invent new information.", g_config.model, &response, err, err_cap);
  msg_list_free(&request);
  if (rc != 0)
    return -1;

  if (!response.content) {
    set_err(err, err_cap, "summary model returned empty content");
    free_llm_response(&response);
    return -1;
  }

  char *summary = xasprintf("Summary:\n%s", response.content);
  char *summary_json = msg_user_json(summary);
  free(summary);
  ctx_replace_range(ctx, 0, prefix_end, summary_json);
  free_llm_response(&response);
  return 0;
}

ContextPolicy summary_policy = {
    .name = "summary",
    .should_apply = summary_should_apply,
    .apply = summary_apply,
};