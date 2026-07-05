# Lab 1: Shell — Programs That Run Programs

Every time you type a command in the terminal, something remarkable happens. The
shell — a seemingly simple text interface — creates a brand new process, replaces
that process with the program you asked for, waits for it to finish, and then
comes back for more. Pipes are even more interesting: `ls | grep foo` wires the
output of one process directly into the input of another, using nothing but file
descriptors.

In this lab, you will build your own shell from scratch.
It's a real REPL (Read-Eval-Print Loop) that can run `ls -la /tmp`, pipe
output through `cat | wc -l`, change directories with `cd`, and recover
gracefully when things go wrong.

---

## 1. What You Are Building

A minimal Unix shell that:

- reads one command per line,
- executes external programs via `fork` + `exec` + `waitpid`,
- connects programs with pipes (`|`),
- handles `cd` and `exit` as built-in commands,
- survives bad input without crashing.

**You submit only `src/shell.c`.** The starter code provides the REPL loop, line
reading, and a `print_prompt()` stub for you to customize. Your job is to
implement `handle_line()`: parse the input, execute commands, and wire up pipes.

You will likely need to write several helper functions — the starter structs
(`command_t`, `pipeline_t`) are suggestions, not requirements. Design the
decomposition that makes sense to you.

### 1.1 Submission boundary and contract

- Do not modify `tests/` or the `Makefile`.
- Out of scope: quoting, escaping, redirection (`<`, `>`, `>>`), background
  jobs (`&`), globbing, and variable expansion.
- Forbidden shortcuts: `system()`, `popen()`, `posix_spawn()`, and `sh -c`.

---

## 2. Before You Start

### 2.1 Build and run

```bash
make          # compile src/shell.c → ./shell
./shell       # run interactively
```

Type `echo hello` and press Enter. Right now the shell prints a TODO message.
Your goal is to make it actually run `echo`.

### 2.2 Run the tests

```bash
make test-a   # Phase A tests only
make test-b   # Phase B tests only
make test-c   # Phase C tests only
make test     # all phases
```

Tests are your specification — they tell you exactly what behavior is
expected in many important cases, but they are not the whole contract. The
handout and starter code comments also define required behavior, and hidden
tests may cover additional edge cases consistent with that contract. Each test
spawns your shell as a subprocess, feeds it commands through stdin, and checks
stdout for expected output.

### 2.3 Use the sanitizer

```bash
make asan          # build with AddressSanitizer
make test-asan     # run tests under AddressSanitizer
```

AddressSanitizer catches memory bugs like buffer overflows, use-after-free, and
double-free. A program that "works" but fails under ASan has a real bug — it
just hasn't bitten you yet.

> **What is AddressSanitizer?** A compiler feature (`-fsanitize=address`) that
> instruments your program to detect memory errors at runtime. The equivalent in
> other languages: Python has no manual memory management (the runtime handles
> it), Rust has the borrow checker (compile-time), Go has a garbage collector. C
> has none of these — ASan is your safety net.

---

## 3. Phase A — Single-Command Execution

### 3.1 The goal

```
$ echo hello
hello
$ nonexistent_cmd
nonexistent_cmd: No such file or directory
$ echo still alive
still alive
```

The shell runs external commands and recovers from failures.

### 3.2 Parsing a single command

Your first task is to split a line like `"echo hello world"` into an argument
array: `{"echo", "hello", "world", NULL}`. This is similar to Python's
`str.split()`, but note that it requires a NULL terminator at the end. For this
lab, splitting on spaces is enough; you do not need quoting or
escaping.

Look up `strtok_r` — it splits a string on delimiters and is the reentrant
(thread-safe) version of `strtok`. The `_r` suffix means "reentrant": it uses
an explicit save pointer instead of hidden global state.

Remember that `exec` requires the argument array to be NULL-terminated.

### 3.3 Running a command: fork + exec + wait

This is the core Unix process model. Three system calls work together:

1. **`fork()`** duplicates the current process. After `fork()`, there are two
   processes running the same code at the same point. The parent receives the
   child's PID; the child receives 0.

2. **`exec()`** replaces the current process image with a new program. If it
   succeeds, it never returns — the old code is gone. If it fails (e.g.,
   command not found), it returns -1.

3. **`wait()`** blocks the parent until the child exits.

Think about what would happen if the shell called `exec` in the parent process
instead of the child: the shell itself would be replaced by the new program and
disappear forever. **`exec` is irreversible** — that is exactly why we `fork`
first.

### 3.4 Critical detail: `_exit()` vs `exit()`

When `exec` fails in the child, you must call `_exit()`, not `exit()`.

`exit()` flushes stdio buffers. But after `fork()`, the child inherited a copy
of the parent's buffered output. If you call `exit()`, that buffer gets flushed
again — you'll see duplicate output or a "ghost shell". `_exit()` terminates
immediately without flushing. In Python, `os._exit()` serves the same purpose.

### 3.5 Error handling

Every system call can fail. When it does, `errno` is set to an error code, and
functions like `perror()` convert it to a human-readable message. This is the C
equivalent of Python's exception messages or Rust's `Result::Err`.

> **Simplifying error reporting.** Manually checking `errno` and formatting
> messages gets tedious. Ask an LLM "how can I simplify error reporting in C?"
> — you will likely learn about `perror`, `strerror`, and the `warn`/`err`
> family from `<err.h>`.

### 3.6 Edge cases

- **Empty lines and whitespace-only lines**: skip silently.
- **Too many arguments**: if `argc >= MAX_ARGS`, report an error and skip.
- **Interactive vs non-interactive**: the starter code uses `isatty()` to detect
  whether stdin is a terminal. When it's not (e.g., tests pipe input to your
  shell), the prompt is suppressed. This is a common Unix convention.

> **`isatty()` and buffering.** libc uses **line buffering** for terminals
> (flush on `\n`) but **full buffering** for files/pipes (flush when the buffer
> is full). The starter calls `fflush(stdout)` after printing the prompt because
> the prompt has no newline. Without the flush, the prompt might not appear.
>
> **Keyword to explore:** "What are the three buffering modes in C stdio?"

---

## 4. Phase B — Pipes

### 4.1 The goal

```
$ echo hello | cat
hello
$ echo hello | cat | wc -l
1
$ | wc
(error message, shell survives)
```

### 4.2 What a pipe actually is

A pipe is a pair of file descriptors: one for writing, one for reading. Data
written to the write end appears at the read end. That's it — a byte stream in
a kernel buffer.

Look up `pipe()`, `dup2()`, and `close()`. `dup2(oldfd, newfd)` makes `newfd`
refer to the same underlying I/O resource as `oldfd` — this is how you redirect
a child's stdin or stdout to a pipe end.

> **The "everything is a file" philosophy.** In Unix, pipes, regular files,
> terminals, and (as you'll see in Lab 2) network sockets all look like file
> descriptors. You `read()` and `write()` to them the same way. `grep` doesn't
> know (or care) whether its input comes from a file, a pipe, or a network
> socket.

### 4.3 The most common pipe bug

If you forget to close the pipe's write end in all processes that don't need it,
the reading process will never see EOF. It will hang forever, waiting for more
data that will never come.

**Rule: after forking, close every pipe fd you don't need.** In the parent,
close both ends after all children are forked. In each child, close all pipe
ends except the one you redirected via `dup2`.

### 4.4 Parsing pipelines

To support `echo hello | cat`, split the line on `|` first, then parse each
segment as a separate command. Note that `|` may appear without surrounding
spaces — `echo hello|cat` is valid.

Before forking anything, validate the pipeline syntax:

- Leading pipe: `| wc` → error
- Trailing pipe: `echo hello |` → error
- Doubled pipe: `echo hello | | wc` → error (empty segment)

### 4.5 General pipeline execution

For N commands, you need N-1 pipes. Think about the general pattern: which
child gets which pipe end for stdin and stdout? Make sure your implementation
handles the general case from the start — hardcoding the two-command case will
cause pain in Phase C.

---

## 5. Phase C — Builtins and Multi-Stage Pipelines

### 5.1 Builtins

`cd` and `exit` are **builtins** — they must execute in the shell process
itself, not in a child.

Think about why: what would happen if `cd` ran as an external program in a
forked child? The child would change _its own_ working directory, then exit.
The shell's directory would be untouched. This is not a C-specific issue — Bash,
Zsh, and Fish all implement `cd` as a builtin for the same reason.

We also require that builtins in pipelines be rejected. Think about why
this makes sense.

### 5.2 Multi-stage pipelines

If your Phase B implementation is general enough, multi-stage pipelines like
`echo hello | cat | cat | wc -l` should work automatically. If you hardcoded
the two-command case, now is the time to generalize.

---

## 6. Key Concepts

### 6.1 The process model: fork + exec + wait

This is the fundamental Unix mechanism for running programs. The three-step
pattern may seem odd — why fork before exec? Because it gives the parent a
chance to set up the child's environment (redirect file descriptors, change
directories, set environment variables) between `fork()` and `exec()`. This
separation of concerns is a deliberate design.

The Coding Agent you will build later in this course uses the same mechanism to
execute tools.

### 6.2 File descriptors

A file descriptor is an integer that refers to an open I/O resource. The kernel
maintains a table of open file descriptors for each process. `fork()` copies the
table; `exec()` preserves it. This is how pipe wiring works — the child inherits
the pipe fds and then redirects stdin/stdout with `dup2`.

### 6.3 The POSIX return-value convention

In C and POSIX, the convention is: return 0 for success, -1 (or negative) for
failure, with the actual error code in `errno`. This is different from languages
that use exceptions (Python, Java) or sum types (Rust's `Result`, Go's multiple
returns).

The pattern of "return a status code, deliver the result through a pointer
parameter" is very common in system APIs. In modern C++, return-value
optimization (RVO) makes returning complex objects by value efficient, so
output-via-pointer is less common. In Rust, `Result<T, E>` is the standard
pattern.

### 6.4 Why C identifiers look strange

You'll encounter names like `strcpy`, `creat`, `atoi`, `ssize_t`,
`STDIN_FILENO`. Why not `string_copy`, `create`, `ascii_to_int`?

Historical reasons: early C compilers had tight limits (6 significant characters
in external names), terminals were 80 columns wide, and naming conventions were
set before modern IDEs existed. `creat` is literally `create` with the `e`
dropped to fit the limit. The `_t` suffix means "type" (a POSIX convention).
You don't need to adopt this style in your own code.

---

## 7. What Comes Next

The shell you just built is the foundation for tool execution in the Coding
Agent you will build later in the course. When an AI agent runs `ls` or `grep`,
it does exactly what your shell does: `fork`, `exec`, read the output, report
the result. The difference is that the agent's "user" is an LLM, not a human at
a terminal.

The concepts from this lab — processes, file descriptors, pipes, error
recovery — will reappear in every subsequent lab and in the final system
integration.
