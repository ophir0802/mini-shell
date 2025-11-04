## Lab C: Mini Shell

`myshell.c` is the main program: a small Unix-like shell with basic built-ins, I/O redirection, and simple piping. `looper.c` is a helper program used for testing signals. `mypipeline.c` is a small standalone demo of piping between two processes.

### Build
- Prerequisites: `gcc`, `make` (Linux/WSL/macOS)
- Build all targets:

```bash
make
```

This produces the following binaries:
- `myshell` – the shell (main program)
- `looper` – signal test helper
- `mypipe` – pipeline demo (built from `mypipeline.c`)

Clean build artifacts:

```bash
make clean
```

### Run myshell (main)
Start the shell:

```bash
./myshell
```

Optional debug logging:

```bash
./myshell -d
```

Supported built-ins (handled inside the shell):
- `quit` – exit the shell
- `cd <dir>` – change directory
- `procs` – show tracked child processes and their status
- `halt <pid>` – send `SIGSTOP` to a process
- `wakeup <pid>` – send `SIGCONT` to a process
- `ice <pid>` – send `SIGINT` to a process

External commands are executed via `execvp`, with support for:
- I/O redirection using `<` and `>`
- Single pipeline chains using `|` (e.g., `ls -l | wc -l`)

### Test helper: looper

`looper` runs an infinite loop and prints when it receives signals, useful to test the shell’s signal commands:

```bash
./looper &            # start in background; note its PID
./myshell
> halt <pid>          # SIGSTOP
> wakeup <pid>        # SIGCONT
> ice <pid>           # SIGINT
```

### Pipeline demo

`mypipe` demonstrates a simple `ls -ls | wc` pipeline using `fork`, `pipe`, and `dup`:

```bash
./mypipe
```

### Notes
- Source files: `myshell.c`, `LineParser.c`, `LineParser.h`, `looper.c`, `mypipeline.c`
- The Makefile target name for the pipeline demo is `mypipe`.


