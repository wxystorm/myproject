/*
 * Tool executor — bridge between "the LLM asked for these N tools" and "here
 * are N tool messages to push into history".
 *
 * Design choices:
 *
 *   - Per-call allocation of result and view arrays. count is bounded by
 *     MAX_TOOL_CALLS (32), so a stack array would also work; the heap
 *     keeps the call frame small and matches the rest of the codebase.
 *
 *   - Ordering by index. Each tool call gets a slot at its request position.
 *     Even if Phase B finishes them out of order, results are written to
 *     the slot, never appended. The agent therefore sees results in
 *     request order without any sort step.
 *
 *   - The UI is owned by this layer, not the agent. The agent has no way to
 *     know how many tools are about to run before unwrapping the LLM's
 *     response, so wiring ui_begin_tools / ui_tool_done here keeps the
 *     reporting next to the work.
 *
 * Phase A: serial loop, one tool at a time, in request order.
 * Phase B: read-only batches run concurrently; mixed batches fall back to
 *          serial. You may reuse the Lab 3 thread pool or create one pthread
 *          per task here. The function signature does not change — the
 *          contract is "results in request order" and that survives either
 *          implementation.
 */
#include "tools/executor.h"

#include "message.h"
#include "tools/thread_pool.h"
#include "tools/tools.h"
#include "ui/ui.h"
#include "util.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    LLMToolCall *call;
    ToolDef *def;
    ToolResult result;
    int index;
} ToolTask;

static void run_one(ToolTask *task) {
    if (task->def) {
        task->result = task->def->exec(task->call->args);
    } else {
        task->result = (ToolResult){
            .ok = false,
            .output = xasprintf("unknown tool: %s", task->call->name ? task->call->name : "(null)"),
        };
    }
    ui_tool_done(task->index, task->result.ok, task->result.output);
}

static void run_one_task(void *arg) {
    run_one((ToolTask *)arg);
}

static bool all_read_only(ToolTask tasks[], int count) {
    for (int i = 0; i < count; i++) {
        if (!tasks[i].def || !tasks[i].def->read_only)
            return false;
    }
    return true;
}

int executor_run_tools(
    LLMToolCall tool_calls[], int count, char *out_msgs[], char *err, size_t err_cap
) {
    if (count <= 0)
        return 0;

    ToolTask *tasks = xmalloc((size_t)count * sizeof(*tasks));
    ToolCallView *views = xmalloc((size_t)count * sizeof(*views));
    char **args_json = xmalloc((size_t)count * sizeof(*args_json));

    memset(out_msgs, 0, (size_t)count * sizeof(*out_msgs));
    for (int i = 0; i < count; i++) {
        tasks[i].call = &tool_calls[i];
        tasks[i].def = tool_find(tool_calls[i].name);
        tasks[i].index = i;
        tasks[i].result = (ToolResult){0};

        args_json[i] = cJSON_PrintUnformatted(tool_calls[i].args);
        if (!args_json[i]) {
            snprintf(err, err_cap, "failed to serialize tool call arguments");
            for (int j = 0; j < i; j++)
                free(args_json[j]);
            free(args_json);
            free(views);
            free(tasks);
            return -1;
        }
        views[i].name = tool_calls[i].name;
        views[i].args_display = args_json[i];
    }

    ui_begin_tools(count, views);

    if (count >= 2 && all_read_only(tasks, count)) {
        thread_pool *pool = thread_pool_create(count);
        if (!pool) {
            snprintf(err, err_cap, "failed to create thread pool");
            for (int i = 0; i < count; i++)
                free(args_json[i]);
            free(args_json);
            free(views);
            free(tasks);
            ui_idle();
            return -1;
        }

        int submitted = 0;
        for (; submitted < count; submitted++) {
            if (thread_pool_submit(pool, run_one_task, &tasks[submitted]) != 0)
                break;
        }

        thread_pool_wait(pool);
        thread_pool_destroy(pool);

        if (submitted != count) {
            snprintf(err, err_cap, "failed to submit tool task to thread pool");
            for (int i = 0; i < count; i++) {
                free(args_json[i]);
                tool_result_free(&tasks[i].result);
            }
            free(args_json);
            free(views);
            free(tasks);
            ui_idle();
            return -1;
        }
    } else {
        for (int i = 0; i < count; i++)
            run_one(&tasks[i]);
    }

    ui_idle();

    int rc = 0;
    for (int i = 0; i < count; i++) {
        const char *content = tasks[i].result.output ? tasks[i].result.output : "(no output)";
        out_msgs[i] = msg_tool_json(tasks[i].call->id ? tasks[i].call->id : "", content);
        if (!out_msgs[i]) {
            snprintf(err, err_cap, "failed to serialize tool result");
            rc = -1;
            break;
        }
    }
    if (rc != 0) {
        for (int i = 0; i < count; i++) {
            free(out_msgs[i]);
            out_msgs[i] = NULL;
        }
    }

    for (int i = 0; i < count; i++) {
        free(args_json[i]);
        tool_result_free(&tasks[i].result);
    }
    free(args_json);
    free(views);
    free(tasks);
    return rc;
}
