#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SYSTEM_PROMPT "You are a helpful assistant."
#define HTTP_PATH "/api/v1/chat/completions"
#define BODY_CAPACITY 16384
#define REQUEST_CAPACITY (BODY_CAPACITY + 512)
#define RESPONSE_CAPACITY (1024 * 1024)
#define PROMPT_CAPACITY 4096

int print_json_string(const char *cursor) {
    while (*cursor != '\0') {
        char ch = *cursor++;

        if (ch == '"') {
            putchar('\n');
            return 0;
        }

        if (ch == '\\') {
            ch = *cursor++;
            if (ch == '\0') {
                return -1;
            }

            switch (ch) {
            case 'n':
                ch = '\n';
                break;
            case 'r':
                ch = '\r';
                break;
            case 't':
                ch = '\t';
                break;
            case '"':
            case '\\':
            case '/':
                break;
            default:
                putchar('\\');
            }
        }

        putchar((unsigned char)ch);
    }

    return -1;
}

int print_assistant_content(const char *json) {
    const char *message;
    const char *content;
    const char *colon;

    message = strstr(json, "\"message\"");
    if (!message)
        return -1;

    content = strstr(message, "\"content\"");
    if (!content)
        return -1;

    colon = strchr(content, ':');
    if (!colon)
        return -1;

    colon++;

    while (*colon == ' ' || *colon == '\t')
        colon++;

    if (*colon != '"')
        return -1;

    return print_json_string(colon + 1);
}

void report_http_error(int status_code) {
    switch (status_code) {
    case 400:
        fprintf(stderr, "HTTP status 400: bad request\n");
        break;
    case 401:
        fprintf(stderr, "HTTP status 401: unauthorized (check API_KEY)\n");
        break;
    case 404:
        fprintf(stderr, "HTTP status 404: endpoint not found\n");
        break;
    default:
        fprintf(stderr, "HTTP status %d\n", status_code);
        break;
    }
}

const char *getenv_default(const char *name, const char *def) {
    const char *v = getenv(name);
    return (v && v[0] != '\0') ? v : def;
}

int read_prompt(char *prompt, size_t prompt_size) {
    if (!fgets(prompt, prompt_size, stdin)) {
        return -1;
    }

    prompt[strcspn(prompt, "\n")] = '\0';
    return prompt[0] == '\0' ? -1 : 0;
}

int build_request(
    const char *host,
    const char *port,
    const char *model,
    const char *api_key,
    const char *user_prompt,
    char *request,
    size_t request_size
) {
    char body[BODY_CAPACITY];
    int body_len;
    int request_len;

    (void)host;
    (void)port;
    (void)model;
    (void)api_key;
    (void)user_prompt;
    (void)request;
    (void)request_size;

    /* Phase A: build the JSON body first, then use its exact length. */
    body[0] = '\0';
    body_len = -1;
    (void)body_len;
    snprintf(body, sizeof(body),
        "{"
        "\"model\": \"%s\","
        "\"messages\":["
        "{\"role\": \"system\", \"content\": \"%s\"},"
        "{\"role\": \"user\", \"content\": \"%s\"}"
        "],"
        "\"stream\": false"
        "}",
        model, SYSTEM_PROMPT, user_prompt);
    //计算body的字节数
    body_len = strlen(body);
    /* Phase A: build the full HTTP request after Content-Length is known. */

    request_len = -1;
    (void)request_len;
    (void)HTTP_PATH;
    snprintf(request, request_size,
    "POST %s HTTP/1.1\r\n"
    "Host: %s:%s\r\n"
    "Authorization: Bearer %s\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: %d\r\n"
    "Connection: close\r\n"
    "\r\n"
    "%s",HTTP_PATH, host, port, api_key, body_len, body);
    request_len = strlen(request);
    return request_len;
}

int connect_to_server(const char *host, const char *port) {
    (void)host;
    (void)port;

    /* Phase A: resolve with getaddrinfo(), try each address, free before return. */
    struct addrinfo *res, *rp;
    int ret;
    ret = getaddrinfo(host, port, &(struct addrinfo){
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
    }, &res);
    if (ret != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
        return -1;
    }
    else
    {
        int fd = -1;
        for (rp = res; rp != NULL; rp = rp->ai_next) {
            fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (fd < 0) {
                continue;
            }
            if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
                break;
            }
            close(fd);
            fd = -1;
        }
        freeaddrinfo(res);
        return fd;
    }

    return -1;
}

int send_all(int fd, const char *data, size_t len) {
    (void)fd;
    (void)data;
    (void)len;
    /* Phase A: one send() is not enough; loop until every byte is written. */
    size_t total_sent = 0;
    while (total_sent < len)
    {
        ssize_t sent = send(fd, data + total_sent, len - total_sent, 0);
        if (sent > 0)
        {
            total_sent += (size_t)sent;

        }
        else if (sent == 0)
        {
            return -1;
        }
        else
        {
            if (errno == EINTR)
            {
                continue;
            }
            return -1;
        }

    }
    return 0;
}

int recv_response(int fd, char *response, size_t response_size) {
    (void)fd;
    (void)response;
    (void)response_size;

    /*
     * Phase B:
     * - append every recv() chunk to the same buffer
     * - stop on EOF from the peer
     * - NUL-terminate before parsing
     */
    size_t total_received = 0;
    
    while (total_received < response_size - 1)
    {
        ssize_t n = recv(fd, response + total_received, response_size - 1 - total_received, 0);
        if (n > 0)
        {
            total_received += (size_t)n;
        }
        else if (n == 0)
        {
            break;
        }
        else
        {
            if (errno == EINTR)
            {
                continue;
            }
            return -1;
        }
        
    }
    response[total_received] = '\0';
    return 0;
}
int parse_response(char *response, int *status_code, char **body) {
    (void)response;
    (void)status_code;
    (void)body;

    /* Phase B: parse the status line first, then find the body at "\r\n\r\n". */
    //先找到状态行
    char *status_line_end = strstr(response, "HTTP/1.1");
    if(!status_line_end)
    {
        return -1;
    }
    char *status_code_line = strchr(status_line_end, ' ');
    if(!status_code_line)
    {
        return -1;
    }
    status_code_line++;
    int status = atoi(status_code_line);
    *status_code = status;
    //找body起始行
    char *body_start = strstr(response, "\r\n\r\n");
    if(!body_start)
    {
        return -1;
    }
    *body = body_start + 4;
    return 0;
}

int main(void) {
    const char *host = getenv_default("LLM_HOST", "127.0.0.1");
    const char *port = getenv_default("LLM_PORT", "18080");
    const char *model = getenv_default("MODEL", "qwen3vl");
    const char *api_key = getenv_default("API_KEY", "sk-DZKTz94nU6Yqe_q1nbw1Rw");
    char prompt[PROMPT_CAPACITY];
    char request[REQUEST_CAPACITY];
    char response[RESPONSE_CAPACITY];
    char *body;
    int request_len;
    int status_code;
    int fd;

    if (read_prompt(prompt, sizeof(prompt)) < 0) {
        fprintf(stderr, "failed to read prompt\n");
        return 1;
    }

    request_len = build_request(host, port, model, api_key, prompt, request, sizeof(request));
    if (request_len < 0) {
        fprintf(stderr, "failed to build request\n");
        return 1;
    }

    fd = connect_to_server(host, port);
    if (fd < 0) {
        fprintf(stderr, "failed to connect\n");
        return 1;
    }

    if (send_all(fd, request, (size_t)request_len) < 0) {
        close(fd);
        fprintf(stderr, "failed to send request\n");
        return 1;
    }

    if (recv_response(fd, response, sizeof(response)) < 0) {
        close(fd);
        fprintf(stderr, "failed to receive response\n");
        return 1;
    }
    close(fd);

    if (parse_response(response, &status_code, &body) < 0) {
        fprintf(stderr, "malformed HTTP response\n");
        return 1;
    }
    if (status_code != 200) {
        report_http_error(status_code);
        return 1;
    }

    if (print_assistant_content(body) < 0) {
        fprintf(stderr, "failed to extract assistant content\n");
        return 1;
    }
    return 0;
}
