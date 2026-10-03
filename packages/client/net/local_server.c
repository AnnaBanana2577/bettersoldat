#include "local_server.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#include <windows.h>
#define SERVER_FILE L"server.exe"
#else
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define SERVER_FILE "server"
#endif

#define QUIT_WAIT_MS 3000 // how long it is given to quit before it is ended

#ifdef _WIN32
// The server's file: the client's own, its name replaced.
static bool server_path(wchar_t *out, size_t cap)
{
    DWORD n = GetModuleFileNameW(NULL, out, (DWORD)cap);
    if (n == 0 || n >= cap) return false;
    wchar_t *slash = wcsrchr(out, L'\\');
    if (!slash || (size_t)(slash + 1 - out) + wcslen(SERVER_FILE) + 1 > cap) return false;
    wcscpy(slash + 1, SERVER_FILE);
    return GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES;
}

bool local_server_start(LocalServer *s, const char *const *args, char *error, size_t error_size)
{
    memset(s, 0, sizeof *s);
    static wchar_t path[MAX_PATH * 4], line[8192];
    if (!server_path(path, sizeof path / sizeof path[0])) {
        snprintf(error, error_size, "no %ls beside the game", SERVER_FILE);
        return false;
    }
    // its command line: the file quoted, then each argument (map and cvar names, which
    // hold no quotes) quoted
    size_t at = (size_t)swprintf(line, sizeof line / sizeof line[0], L"\"%ls\"", path);
    for (int i = 0; args && args[i]; i++) {
        wchar_t arg[1024];
        if (MultiByteToWideChar(CP_UTF8, 0, args[i], -1, arg, (int)(sizeof arg / sizeof arg[0])) <= 0) continue;
        int w = swprintf(line + at, sizeof line / sizeof line[0] - at, L" \"%ls\"", arg);
        if (w > 0) at += (size_t)w;
    }

    // its console on two pipes, the child's ends inherited and ours not
    SECURITY_ATTRIBUTES inherit = {.nLength = sizeof inherit, .bInheritHandle = TRUE};
    HANDLE in_read, in_write, out_read, out_write;
    if (!CreatePipe(&in_read, &in_write, &inherit, 0)) {
        snprintf(error, error_size, "no pipe for its console");
        return false;
    }
    if (!CreatePipe(&out_read, &out_write, &inherit, 0)) {
        CloseHandle(in_read), CloseHandle(in_write);
        snprintf(error, error_size, "no pipe for its console");
        return false;
    }
    SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup = {.cb = sizeof startup, .dwFlags = STARTF_USESTDHANDLES, .hStdInput = in_read,
                            .hStdOutput = out_write, .hStdError = out_write};
    PROCESS_INFORMATION process;
    // suspended until it is in the job, so it never runs untied to the game
    BOOL made = CreateProcessW(path, line, NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, NULL, NULL, &startup, &process);
    CloseHandle(in_read), CloseHandle(out_write);
    if (!made) {
        CloseHandle(in_write), CloseHandle(out_read);
        snprintf(error, error_size, "%ls wouldn't start (error %lu)", SERVER_FILE, (unsigned long)GetLastError());
        return false;
    }
    HANDLE job = CreateJobObjectW(NULL, NULL);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE; // gone with the game
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits);
        AssignProcessToJobObject(job, process.hProcess);
    }
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    s->process = process.hProcess;
    s->job = job;
    s->input = in_write;
    s->output = out_read;
    s->running = true;
    return true;
}

// What has come of its output, up to `size` bytes, without waiting; 0 for nothing.
static size_t read_some(LocalServer *s, char *out, size_t size)
{
    DWORD available = 0, got = 0;
    if (!PeekNamedPipe(s->output, NULL, 0, NULL, &available, NULL) || available == 0) return 0;
    if (!ReadFile(s->output, out, available < size ? available : (DWORD)size, &got, NULL)) return 0;
    return got;
}

bool local_server_alive(LocalServer *s)
{
    return s->running && WaitForSingleObject(s->process, 0) == WAIT_TIMEOUT;
}

void local_server_send(LocalServer *s, const char *text)
{
    if (!s->running) return;
    DWORD wrote;
    WriteFile(s->input, text, (DWORD)strlen(text), &wrote, NULL);
    WriteFile(s->input, "\n", 1, &wrote, NULL);
}

void local_server_stop(LocalServer *s)
{
    if (!s->running) return;
    local_server_send(s, "quit");
    if (WaitForSingleObject(s->process, QUIT_WAIT_MS) != WAIT_OBJECT_0) TerminateProcess(s->process, 1);
    CloseHandle(s->input), CloseHandle(s->output), CloseHandle(s->process);
    if (s->job) CloseHandle(s->job);
    memset(s, 0, sizeof *s);
}
#else
static bool server_path(char *out, size_t cap)
{
    ssize_t n = readlink("/proc/self/exe", out, cap - 1);
    if (n <= 0) return false;
    out[n] = '\0';
    char *slash = strrchr(out, '/');
    if (!slash || (size_t)(slash + 1 - out) + strlen(SERVER_FILE) + 1 > cap) return false;
    strcpy(slash + 1, SERVER_FILE);
    return access(out, X_OK) == 0;
}

bool local_server_start(LocalServer *s, const char *const *args, char *error, size_t error_size)
{
    memset(s, 0, sizeof *s);
    s->input = s->output = -1;
    char path[PATH_MAX];
    if (!server_path(path, sizeof path)) {
        snprintf(error, error_size, "no %s beside the game", SERVER_FILE);
        return false;
    }
    int count = 0;
    while (args && args[count]) count++;
    char **argv = calloc((size_t)count + 2, sizeof *argv);
    int in[2], out[2];
    if (!argv || pipe(in) != 0) {
        free(argv);
        snprintf(error, error_size, "no pipe for its console");
        return false;
    }
    if (pipe(out) != 0) {
        close(in[0]), close(in[1]), free(argv);
        snprintf(error, error_size, "no pipe for its console");
        return false;
    }
    argv[0] = path;
    for (int i = 0; i < count; i++) argv[i + 1] = (char *)args[i];
    signal(SIGPIPE, SIG_IGN); // a write to a server gone is an error, not the game's end
    pid_t pid = fork();
    if (pid == 0) {
        prctl(PR_SET_PDEATHSIG, SIGTERM); // gone with the game
        dup2(in[0], 0), dup2(out[1], 1), dup2(out[1], 2);
        close(in[0]), close(in[1]), close(out[0]), close(out[1]);
        execv(path, argv);
        _exit(127);
    }
    free(argv);
    close(in[0]), close(out[1]);
    if (pid < 0) {
        close(in[1]), close(out[0]);
        snprintf(error, error_size, "%s wouldn't start", SERVER_FILE);
        return false;
    }
    fcntl(out[0], F_SETFL, fcntl(out[0], F_GETFL) | O_NONBLOCK);
    s->pid = pid;
    s->input = in[1];
    s->output = out[0];
    s->running = true;
    return true;
}

static size_t read_some(LocalServer *s, char *out, size_t size)
{
    ssize_t got = read(s->output, out, size);
    return got > 0 ? (size_t)got : 0;
}

bool local_server_alive(LocalServer *s)
{
    return s->running && waitpid(s->pid, NULL, WNOHANG) == 0;
}

void local_server_send(LocalServer *s, const char *text)
{
    if (!s->running) return;
    if (write(s->input, text, strlen(text)) < 0 || write(s->input, "\n", 1) < 0) return;
}

void local_server_stop(LocalServer *s)
{
    if (!s->running) return;
    local_server_send(s, "quit");
    bool gone = false;
    for (int ms = 0; ms < QUIT_WAIT_MS && !gone; ms += 10) {
        gone = waitpid(s->pid, NULL, WNOHANG) != 0;
        if (!gone) nanosleep(&(struct timespec){0, 10 * 1000000L}, NULL);
    }
    if (!gone) {
        kill(s->pid, SIGKILL);
        waitpid(s->pid, NULL, 0);
    }
    close(s->input), close(s->output);
    memset(s, 0, sizeof *s);
}
#endif

bool local_server_line(LocalServer *s, char *line, size_t size)
{
    if (!s->running) return false;
    for (;;) {
        char *end = memchr(s->partial, '\n', s->partial_len);
        if (end || s->partial_len == sizeof s->partial) { // a line, or as much as one holds
            size_t len = end ? (size_t)(end - s->partial) : s->partial_len;
            size_t taken = end ? len + 1 : len;
            if (len > 0 && s->partial[len - 1] == '\r') len--;
            snprintf(line, size, "%.*s", (int)len, s->partial);
            memmove(s->partial, s->partial + taken, s->partial_len - taken);
            s->partial_len -= taken;
            return true;
        }
        size_t got = read_some(s, s->partial + s->partial_len, sizeof s->partial - s->partial_len);
        if (got == 0) return false;
        s->partial_len += got;
    }
}
