# Lab 2: LLM Client — Talking to a Remote Service

When you call `openai.ChatCompletion.create()` in Python, or click "Send" in
ChatGPT, what actually happens? Your computer opens a TCP connection to a
server, sends an HTTP request as a carefully formatted text string, and reads
back an HTTP response — also just text. Libraries hide all of this behind a
one-line API call, but underneath, it is sockets, byte streams, and protocol
parsing.

In this lab, you will build an HTTP client in C that talks to an LLM API. You will
construct the HTTP request byte by byte, send it over a raw TCP socket, receive
the response, and extract the assistant's reply.

---

## 1. See It Work First

Before writing any code, try talking to the LLM API with `curl` (replace `YOUR_API_KEY` with your actual key):

```bash
curl http://127.0.0.1:18080/api/v1/chat/completions \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer YOUR_API_KEY" \
  -d '{
    "model": "qwen3coder",
    "messages": [
      {"role": "system", "content": "You are a helpful assistant."},
      {"role": "user", "content": "say hello"}
    ],
    "stream": false
  }'
```

You should see a JSON response with the assistant's reply. Your job in this lab
is to do exactly what `curl` does here — but in C, with raw sockets.

### 1.1 Setting up the proxy

You already have an API key (if not, go to apply for the
[Zhiyuan 1 API](https://form.sjtu.edu.cn/infoplus/form/net_ai_api_apply/start?locale=zh)).
The LLM API endpoint uses HTTPS,
but this lab focuses on HTTP and sockets — we don't want to deal with TLS. The
solution: run a local reverse proxy that accepts plain HTTP and forwards to the
HTTPS endpoint.

Install (by `apt`) and start [Caddy](https://caddyserver.com/):

```bash
caddy reverse-proxy --from :18080 --to https://models.sjtu.edu.cn
```

Now `127.0.0.1:18080` speaks plain HTTP and forwards to the real API. Your C
program connects to this local address.

### 1.2 The mock server

For testing and debugging, the lab also provides a mock server that returns
pre-scripted responses:

```bash
python3 tools/mock_server.py
```

The mock server listens on the same `127.0.0.1:18080` by default. Use it when
you want deterministic, reproducible behavior — the automated tests use it
exclusively. Switch to the Caddy proxy when you want to see real LLM responses.

---

## 2. What You Are Building

A command-line program that:

1. reads one line of text from stdin (the user's prompt),
2. constructs an HTTP POST request with a JSON body,
3. connects to a server via TCP,
4. sends the request and receives the response,
5. parses the HTTP response and prints the assistant's reply.

**You submit only `src/llm.c`.** The starter code provides helper functions for
JSON string printing, assistant content extraction, error reporting, and
environment variable loading. Your job is to implement functions:
`build_request()`, `connect_to_server()`, `send_all()`, `recv_response()`, and
`parse_response()`.

### 2.1 Submission boundary and contract

- Do not modify `tests/`, `tools/`, or the `Makefile`.
- Read exactly one prompt line from `stdin`.
- Empty input is an error.
- Tests use ordinary printable characters. You do not need to support `"`,
  `\`, or other prompt characters that would require JSON string escaping.
- Print errors to `stderr` and return non-zero on malformed input, malformed
  HTTP, connection failure, or non-200 HTTP status.

### 2.2 Environment variables

Your program reads configuration from environment variables with defaults:

| Variable   | Default        | Purpose                      |
| ---------- | -------------- | ---------------------------- |
| `LLM_HOST` | `127.0.0.1`    | Server hostname              |
| `LLM_PORT` | `18080`        | Server port                  |
| `MODEL`    | `qwen3coder`   | Model name for the JSON body |
| `API_KEY`  | `YOUR_API_KEY` | Authorization token          |

> **Environment variables as configuration.** This is a very common pattern in
> server-side software. It lets
> you switch between the mock server (testing) and a real server (development)
> without changing code. In Python: `os.environ.get("LLM_HOST", "127.0.0.1")`.

### 2.3 Build and test

```bash
make          # build
make test-a   # Phase A
make test-b   # Phase B
make test     # all phases
make asan     # AddressSanitizer build
make test-asan
```

### 2.4 Read the starter code

Before writing anything, read `src/llm.c`. The `main()` function shows the
entire flow: `read_prompt` → `build_request` → `connect_to_server` → `send_all`
→ `recv_response` → `parse_response` → `print_assistant_content`. The last
function is already provided — it does minimal JSON field extraction to find the
assistant's reply. You may add private helper functions inside `src/llm.c`, but
you do not need a full HTTP parser or a JSON library for this lab.

---

## 3. Phase A — Minimal End-to-End Success

### 3.1 The goal

```bash
echo "say hello" | ./llm
# Output: the assistant's reply text
```

You need to implement `build_request()`, `connect_to_server()`, and
`send_all()`. Phase A tests use simple, well-formed responses, so a basic
`recv_response()` that reads a single chunk may work for now (but will fail in
Phase B).

### 3.2 Building the HTTP request

HTTP is a text protocol. A request looks like this:

```
POST /api/v1/chat/completions HTTP/1.1\r\n
Host: 127.0.0.1:18080\r\n
Content-Type: application/json\r\n
Authorization: Bearer YOUR_API_KEY\r\n
Content-Length: 142\r\n
Connection: close\r\n
\r\n
{"model":"qwen3coder","messages":[{"role":"system","content":"You are a helpful assistant."},{"role":"user","content":"say hello"}],"stream":false}
```

Key observations:

- **Every header line ends with `\r\n`** (carriage return + newline). This is
  the HTTP standard, inherited from the teletype era. Unix uses `\n` alone, but
  HTTP uses `\r\n`.
- **A blank line (`\r\n\r\n`) separates headers from the body.** This is how
  the receiver knows where headers end.
- **`Content-Length` must exactly match the body size in bytes.** If it's wrong,
  the server will reject the request or read the wrong amount of data.
- **`Connection: close`** tells the server to close the connection after
  responding. This simplifies your code: you read until EOF.

**Strategy:** Build the JSON body first with `snprintf`, measure its length,
then build the full HTTP request using that length for `Content-Length`.

> **`snprintf` and buffer safety.** `snprintf(buf, size, fmt, ...)` writes at
> most `size - 1` characters and always NUL-terminates. It returns the number of
> characters that _would have been written_ without truncation. If the return
> value >= `size`, the output was truncated. The `n` in `snprintf` is what makes
> it safe compared to the dangerous `sprintf`.

> **Why hand-build HTTP?** In practice, you'd use `libcurl` (C), `requests`
> (Python), or `fetch` (JavaScript). We're building it by hand so you understand
> what those libraries do internally.

### 3.3 Connecting to the server

TCP connection setup uses three functions:

1. **`getaddrinfo()`** — resolves hostname and port into socket addresses.
   Returns a linked list of possible addresses (a hostname might resolve to
   multiple IPs, or support both IPv4 and IPv6).

2. **`socket()`** — creates a TCP socket. Returns a file descriptor.

3. **`connect()`** — establishes the TCP connection.

The standard pattern: iterate through `getaddrinfo` results, try each address
until one succeeds. Don't forget to call `freeaddrinfo()` when done — it
allocates memory that you must release.

> **`getaddrinfo` replaces direct IP manipulation.** Old code used
> `inet_aton()` and hardcoded `struct sockaddr_in`. `getaddrinfo` is the modern,
> protocol-independent way. It handles DNS lookup, IPv4/IPv6, and address format
> in one call.
>
> **Keyword to explore:** "What does `getaddrinfo` do and why is it better than
> `gethostbyname`?"

> **The return-value-plus-output-pointer pattern.** `getaddrinfo` returns an
> error code (0 for success) and delivers its result through a pointer parameter.
> Its errors use `gai_strerror()` instead of `perror()` because they are not
> stored in `errno`.

### 3.4 Sending the request

A single `send()` call may not transmit all your data. The kernel might accept
only a portion — this is normal for TCP. You must loop until every byte is
written.

Also handle `EINTR`: if a signal arrives during `send()`, the call returns -1
with `errno == EINTR`. This doesn't mean the operation failed — it means "try
again."

> **TCP is a byte stream, not a message protocol.** When you call `send()` with
> 1000 bytes, the kernel might accept only 500 this time. You must track how
> much was sent and continue from where you left off. The same applies to
> `recv()`.

---

## 4. Phase B — Robust Response Handling

### 4.1 The goal

Handle edge cases that break naive implementations:

- Responses split across many tiny `recv()` calls
- Very large responses
- Non-200 HTTP status codes (e.g., 500 Internal Server Error)
- Malformed HTTP responses
- Responses without a `Content-Length` header

### 4.2 Receiving the response

The response arrives as a stream of bytes. One `recv()` call might return 5
bytes, or 5000. You must accumulate everything in a buffer until the connection
closes (because we sent `Connection: close`).

Key points:

- **`recv()` returning 0 means EOF** — the peer closed the connection. This is
  how you know the response is complete when there's no `Content-Length`.
- **NUL-terminate the buffer** so you can use string functions on it.
- **Handle `EINTR`** the same way as in `send_all`.

### 4.3 Parsing the response

An HTTP response looks like:

```
HTTP/1.1 200 OK\r\n
Content-Type: application/json\r\n
\r\n
{"id":"...","choices":[{"message":{"content":"Hello!"}}]}
```

You need to extract two things:

1. **The status code** from the first line.
2. **The body**, which starts right after the `\r\n\r\n` boundary.

Look up `strchr` and `strstr` — they are your tools for finding characters and
substrings in the response buffer.

### 4.4 Understanding the `\r\n\r\n` boundary

The header/body boundary is exactly the sequence `\r\n\r\n` — four bytes. A
common bug: this sequence spans two `recv()` calls. If you parse headers after
each individual `recv()`, you'll miss the boundary. The solution: accumulate the
entire response first, then parse.

> **`\r\n` vs `\n`.** Unix uses `\n` (LF) as the line ending. HTTP, email
> (SMTP), and many internet protocols use `\r\n` (CR+LF) because they were
> designed in an era when terminals needed both a "carriage return" and a "line
> feed." Your shell in Lab 1 deals with `\n`; your HTTP client must deal with
> `\r\n`.

### 4.5 Edge cases

- **Non-200 status:** Check the status code before trying to extract content.
  The starter provides `report_http_error()` for common codes.
- **Malformed HTTP:** If your string search functions return NULL, the response
  is broken. Return -1.
- **No `Content-Length`:** Because we use `Connection: close`, just read until
  EOF. You don't need to parse `Content-Length` at all.

---

## 5. Key Concepts

### 5.1 TCP is a byte stream

This is the single most important lesson. TCP guarantees that bytes arrive in
order and without corruption, but it makes no promises about _how many bytes_
arrive in each `recv()` call. Your code must handle any fragmentation pattern.

This is fundamentally different from sending a message in a chat app or calling
a function — there are no "message boundaries" in TCP. The application layer
(HTTP, in our case) defines where one message ends and the next begins.

### 5.2 HTTP is a text protocol

HTTP/1.1 is just formatted text over TCP. You can debug it with `nc` (netcat):

```bash
echo -e "GET / HTTP/1.1\r\nHost: cs.sjtu.edu.cn\r\nConnection: close\r\n\r\n" | nc cs.sjtu.edu.cn 80
```

### 5.3 The serialization problem

Your program converts structured data (model name, messages) into a text format
(JSON in HTTP), sends it as bytes, and parses a text response back into
structured data. This process — **serialization** and **deserialization** — is
fundamental to all networked software.

In this lab, you do it manually with `snprintf`. The pain of manual
serialization motivates why JSON libraries, Protocol Buffers,
and other serialization frameworks exist.

### 5.4 The socket API

The progression `getaddrinfo → socket → connect → send → recv → close` is the
universal TCP client pattern. Every language has a wrapper:

| C                                    | Python                        | Go                             |
| ------------------------------------ | ----------------------------- | ------------------------------ |
| `getaddrinfo` + `socket` + `connect` | `socket.create_connection()`  | `net.Dial()`                   |
| `send` / `recv`                      | `sock.send()` / `sock.recv()` | `conn.Write()` / `conn.Read()` |
| `close`                              | `sock.close()`                | `conn.Close()`                 |

### 5.5 `errno` as thread-local state

`errno` looks like a global variable (indeed, it used to be one), but it is actually a macro that expands to
a thread-local value. This was necessary when multithreading became common — a
global `errno` would be a data race.

---

## 6. What Comes Next

The HTTP client you just built is the communication layer for the Coding Agent.
Every time the agent asks the LLM for a decision, it does what your program
does: construct an HTTP request, send it, parse the response. In later stages,
the request body will grow more complex (multi-turn conversation history, tool
definitions), and you will introduce a JSON library to replace the manual
`snprintf` approach.
