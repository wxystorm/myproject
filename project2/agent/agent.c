/*
 * agent.c — orchestration between user input, LLM turns, and tool execution.
 *
 * The skeleton below is sized for Phase A: one request in, one request out,
 * no persistent state to speak of. Phase B and Phase C will both require
 * you to revisit `struct Agent`, agent_create, and agent_free — treat what
 * is here as a starting point, not a contract.
 */
#include "agent.h"

#include "config.h"
#include "context/context.h"
#include "llm_client.h"
#include "message.h"
#include "tools/tools.h"
#include "util.h"
#include "tools/executor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char AGENT_SYSTEM_TEMPLATE[] =
    "You are a coding agent running in the CLI at %s.\n"
    "Use the provided tools when you need to run shell commands.\n"
    "Return a short, final text reply when the task is done.";

struct Agent {
  Context *ctx;
  char *system_prompt;
  char *last_reply;
};

static void free_llm_response(LLMResponse *response) {
  if (!response)
    return;
  if (response->tool_calls) {
    for (int i = 0; i < response->n_tool_calls; i++) {
      free(response->tool_calls[i].id);
      free(response->tool_calls[i].name);
      cJSON_Delete(response->tool_calls[i].args);
    }
    free(response->tool_calls);
  }
  free(response->content);
  response->content = NULL;
  free(response->raw_message);
  response->raw_message = NULL;
  response->tool_calls = NULL;
  response->n_tool_calls = 0;
}

Agent *agent_create(void) {
  Agent *a = calloc(1, sizeof(*a));
  if (!a)
    return NULL;
  tools_init();
  a->ctx = ctx_create(g_config.context_window);
  if (!a->ctx) {
    free(a);
    return NULL;
  }
  ctx_add_policy(a->ctx, &offload_policy);
  ctx_add_policy(a->ctx, &summary_policy);
  a->system_prompt = xasprintf(AGENT_SYSTEM_TEMPLATE, g_config.workdir);
  return a;
}

void agent_free(Agent *a) {
  if (!a)
    return;
  ctx_free(a->ctx);
  free(a->system_prompt);
  free(a->last_reply);
  free(a);
}

const char *agent_chat(Agent *a, const char *user_input) {
  if (!a || !a->ctx)
    return NULL;

  ctx_push(a->ctx, msg_user_json(user_input));

  char err[256];
  enum { MAX_TURNS = 20 };

  for (int turn = 0; turn < MAX_TURNS; turn++) {
    if (ctx_reclaim(a->ctx, err, sizeof(err)) != 0) {
      fprintf(stderr, "ctx_reclaim failed: %s\n", err);
      return NULL;
    }

    LLMResponse response = {0};
    if (llm_chat(ctx_history(a->ctx), a->system_prompt, g_config.model, &response, err,
                 sizeof(err)) != 0) {
      fprintf(stderr, "llm_chat failed: %s\n", err);
      return NULL;
    }

    if (response.n_tool_calls == 0) {
      free(a->last_reply);
      a->last_reply = response.content;
      response.content = NULL;
      if (response.raw_message) {
        ctx_push(a->ctx, response.raw_message);
        response.raw_message = NULL;
      }
      free_llm_response(&response);
      return a->last_reply;
    }

    if (response.raw_message) {
      ctx_push(a->ctx, response.raw_message);
      response.raw_message = NULL;
    }

    char **tool_messages = xmalloc((size_t)response.n_tool_calls * sizeof(*tool_messages));
    memset(tool_messages, 0, (size_t)response.n_tool_calls * sizeof(*tool_messages));

    if (executor_run_tools(response.tool_calls, response.n_tool_calls,
                           tool_messages, err, sizeof(err)) != 0) {
      fprintf(stderr, "executor_run_tools failed: %s\n", err);
      for (int i = 0; i < response.n_tool_calls; i++) {
        free(tool_messages[i]);
      }
      free(tool_messages);
      free_llm_response(&response);
      return NULL;
    }

    for (int i = 0; i < response.n_tool_calls; i++) {
      ctx_push(a->ctx, tool_messages[i]);
    }
    free(tool_messages);

    free_llm_response(&response);
  }

  fprintf(stderr, "agent_chat: exceeded max turns\n");
  return NULL;
}
//int llm_chat(const MessageList *messages, const char *system_prompt,
//            const char *model, LLMResponse *out, char *err, size_t err_cap) 