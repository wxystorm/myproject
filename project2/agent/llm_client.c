/*
 * llm_client.c — HTTP+JSON glue between the Agent and the LLM service.
 *
 * Your job: implement llm_chat. Everything else in this file is yours to
 * design. You will certainly want helpers (request construction, response
 * parsing, …); whether and how you decompose them is a decision for you.
 */
#include "llm_client.h"

#include "config.h"
#include "http.h"
#include "tools/tools.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "util.h"

#define LLM_TIMEOUT_SEC 120

int llm_chat(const MessageList *messages, const char *system_prompt,
             const char *model, LLMResponse *out, char *err, size_t err_cap) {
  (void)messages;
  (void)system_prompt;
  (void)model;
  (void)out;

  /*
   * TODO(student, Part 1A):
   *
   * 1. Build the request body. It is a JSON object with fields
   *    `model`, `messages` (system prompt prepended to the given list),
   *    `tools` (one entry describing the bash tool — see BASH_TOOL_NAME /
   *    BASH_TOOL_DESC / BASH_TOOL_SCHEMA in tools/tools.h), and
   *    `max_tokens` (g_config.max_tokens). Use cJSON (see libs/cJSON.h);
   *    hand-splicing strings will not scale.
   *
   * 2. Open a TCP connection (see http.h) and send a POST to
   *    /api/v1/chat/completions with Authorization: Bearer <api_key>.
   *
   * 3. Read the full response with recv_all, parse it with
   *    http_parse_response, and reject any non-200 status.
   *
   * 4. Parse the JSON body. The assistant message lives at
   *    choices[0].message. Extract `content` (may be missing or empty)
   *    and every entry of `tool_calls` (each has `id`, `function.name`,
   *    `function.arguments`). `arguments` arrives as a JSON *string* on
   *    the wire — cJSON_Parse it back into an object. If the string is
   *    empty, treat it as an empty object.
   *
   * 5. Also keep a serialized copy of the whole assistant message
   *    (cJSON_PrintUnformatted is convenient) in out->raw_message — the
   *    agent will push it into history verbatim so the LLM sees its own
   *    previous reply on the next call.
   *
   * Everything you malloc / cJSON_Parse here has to be freed somewhere.
   * Decide where. `make asan` will tell you if you got it wrong.
   */
  //消息历史+系统提示 -> JSON请求体
  cJSON *request_json = cJSON_CreateObject();
  cJSON_AddStringToObject(request_json, "model", model);
  cJSON_AddNumberToObject(request_json, "max_tokens", g_config.max_tokens);

  // 1.1 组装 messages 数组
  cJSON *messages_json = cJSON_CreateArray();
  
  // 首先放入 system prompt
  if (system_prompt) {
    cJSON *sys_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_msg, "role", "system");
    cJSON_AddStringToObject(sys_msg, "content", system_prompt);
    cJSON_AddItemToArray(messages_json, sys_msg);
  }
  
  // 接着放入历史对话（需要将原有的 JSON 字符串解析回对象塞入数组）
  for (size_t i = 0; i < messages->len; ++i) {       
    cJSON *msg_obj = cJSON_Parse(messages->items[i]);
    if (msg_obj) {
      cJSON_AddItemToArray(messages_json, msg_obj);
    }
  }
  cJSON_AddItemToObject(request_json, "messages", messages_json);

  // 1.2 组装 tools 数组 (Phase A 核心：告诉模型我们有 bash 工具)
 cJSON *tools_json = cJSON_CreateArray();

int tool_count = 0;
ToolDef *const *defs = tool_list(&tool_count);

for (int i = 0; i < tool_count; i++) {
  ToolDef *def = defs[i];

  cJSON *tool_obj = cJSON_CreateObject();
  cJSON_AddStringToObject(tool_obj, "type", "function");

  cJSON *function_obj = cJSON_CreateObject();
  cJSON_AddStringToObject(function_obj, "name", def->name);
  cJSON_AddStringToObject(function_obj, "description", def->desc);

  cJSON *parameters = cJSON_Parse(def->param_schema);
  if (!parameters) {
    cJSON_Delete(tool_obj);
    cJSON_Delete(tools_json);
    cJSON_Delete(request_json);
    snprintf(err, err_cap, "failed to parse schema for tool: %s", def->name);
    return -1;
  }

  cJSON_AddItemToObject(function_obj, "parameters", parameters);
  cJSON_AddItemToObject(tool_obj, "function", function_obj);
  cJSON_AddItemToArray(tools_json, tool_obj);
}

cJSON_AddItemToObject(request_json, "tools", tools_json);

  // ==========================================
  // 2. 发送 HTTP 请求
  // ==========================================
  char *request_body = cJSON_PrintUnformatted(request_json);
  cJSON_Delete(request_json);

  // 2.1 按照 HTTP 协议规范，使用 xasprintf 拼接请求头
  // 注意：主机名、端口号、API KEY 都要从全局配置 g_config 中读取
  char *http_header = xasprintf(
      "POST /api/v1/chat/completions HTTP/1.1\r\n"
      "Host: %s:%d\r\n"
      "Authorization: Bearer %s\r\n"
      "Content-Type: application/json\r\n"
      "Content-Length: %zu\r\n"
      "Connection: close\r\n\r\n",
      g_config.llm_host, g_config.llm_port, g_config.api_key, strlen(request_body));

  // 2.2 建立 TCP 连接 (传入 err 和 err_cap 顺装错误)
  int sock_fd = tcp_connect(g_config.llm_host, g_config.llm_port, err, err_cap);
  if (sock_fd < 0) {
    free(request_body);
    free(http_header);
    return -1;
  }

  // 2.3 发送请求头和请求体
  if (send_all(sock_fd, http_header, strlen(http_header)) != 0 ||
      send_all(sock_fd, request_body, strlen(request_body)) != 0) {
    free(request_body);
    free(http_header);
    close(sock_fd);
    snprintf(err, err_cap, "Failed to send HTTP request data via send_all");
    return -1;
  }
  free(request_body);
  free(http_header);

  // 2.4 接收完整的原始 HTTP 响应
  char *raw_response = NULL;
  size_t raw_response_len = 0;
  if (recv_all(sock_fd, LLM_TIMEOUT_SEC, &raw_response, &raw_response_len, err, err_cap) != 0) {
    close(sock_fd);
    return -1; 
  }
  close(sock_fd); // 读完及时释放 socket 描述符

  // 2.5 解析 HTTP 协议响应头
  int status_code = 0;
  const char *http_body_start = NULL;
  if (http_parse_response(raw_response, &status_code, &http_body_start) != 0) {
    free(raw_response);
    snprintf(err, err_cap, "Failed to parse HTTP protocol framing");
    return -1;
  }

  // 2.6 拒绝所有非 200 的状态码
  if (status_code != 200) {
    free(raw_response);
    snprintf(err, err_cap, "LLM service returned non-200 status code: %d", status_code);
    return -1;
  }

  // ==========================================
  // 3. 解析 JSON 响应体
  // ==========================================
  // 因为 http_parse_response 返回的 http_body_start 指向的是 raw_response 内部的某一段位置，
  // 我们可以直接把这个指针丢给 cJSON_Parse 运行
  cJSON *response_json = cJSON_Parse(http_body_start);
  
  // 此时 raw_response 已经没用了，可以安全 free 掉它，防止后续分支遗漏导致内存泄漏
  free(raw_response);

  if (!response_json) {
    snprintf(err, err_cap, "Failed to parse LLM response body as JSON");
    return -1;
  }

  cJSON *choices = cJSON_GetObjectItem(response_json, "choices");
  if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
    cJSON_Delete(response_json);
    snprintf(err, err_cap, "LLM response JSON does not contain choices");
    return -1;
  }

  cJSON *message = cJSON_GetObjectItem(cJSON_GetArrayItem(choices, 0), "message");
  if (!message || !cJSON_IsObject(message)) {
    cJSON_Delete(response_json);
    snprintf(err, err_cap, "LLM response JSON does not contain a valid message");
    return -1;
  }

  // 提取 content 文本内容
  cJSON *content = cJSON_GetObjectItem(message, "content");
  out->content = (content && cJSON_IsString(content)) ? xstrdup(content->valuestring) : xstrdup("");

  // 提取序列化的 raw_message 供 Agent 历史记录使用
  out->raw_message = cJSON_PrintUnformatted(message);

  // 提取 tool_calls
  cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
  if (tool_calls && cJSON_IsArray(tool_calls)) {
    out->n_tool_calls = cJSON_GetArraySize(tool_calls); // 修正字段名
    out->tool_calls = malloc(out->n_tool_calls * sizeof(LLMToolCall)); // 修正类型
    
    for (int i = 0; i < out->n_tool_calls; ++i) {
      cJSON *tool_call_json = cJSON_GetArrayItem(tool_calls, i);
      cJSON *id = cJSON_GetObjectItem(tool_call_json, "id");
      cJSON *function = cJSON_GetObjectItem(tool_call_json, "function");
      
      if (!id || !cJSON_IsString(id) || !function || !cJSON_IsObject(function)) {
        // 简单防错处理，实际项目中最好做深层清理
        out->n_tool_calls = 0;
        free(out->tool_calls);
        out->tool_calls = NULL;
        cJSON_Delete(response_json);
        snprintf(err, err_cap, "Invalid tool call format");
        return -1;
      }
      
      cJSON *function_name = cJSON_GetObjectItem(function, "name");
      cJSON *arguments = cJSON_GetObjectItem(function, "arguments");
      
      out->tool_calls[i].id = xstrdup(id->valuestring);
      out->tool_calls[i].name = xstrdup(function_name ? function_name->valuestring : ""); // 修正字段名
      
      // 依照讲义处理空参数边界情况
      cJSON *arguments_json = NULL;
      if (arguments && cJSON_IsString(arguments) && strlen(arguments->valuestring) > 0) {
        arguments_json = cJSON_Parse(arguments->valuestring);
      }
      
      // 如果 arguments 为空或解析失败，视为空对象 {}
      if (!arguments_json) {
        arguments_json = cJSON_CreateObject();
      }
      out->tool_calls[i].args = arguments_json; // 修正字段名
    }
  } else {
    out->n_tool_calls = 0;
    out->tool_calls = NULL;
  }

  cJSON_Delete(response_json);
  return 0; // 成功必须返回 0！
}
