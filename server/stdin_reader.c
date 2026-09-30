#include "stdin_reader.h"

#include <stdio.h>
#include <string.h>

#define STDIN_LINES 8

#ifdef _WIN32
#define NOGDI
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef CRITICAL_SECTION Lock;
static void lock_init(Lock *l) { InitializeCriticalSection(l); }
static void lock(Lock *l) { EnterCriticalSection(l); }
static void unlock(Lock *l) { LeaveCriticalSection(l); }
#else
#include <pthread.h>
typedef pthread_mutex_t Lock;
static void lock_init(Lock *l) { pthread_mutex_init(l, NULL); }
static void lock(Lock *l) { pthread_mutex_lock(l); }
static void unlock(Lock *l) { pthread_mutex_unlock(l); }
#endif

// The lines read and not yet taken, a ring; a line that arrives while it is full is lost.
static struct {
    Lock lock;
    char lines[STDIN_LINES][STDIN_LINE_SIZE];
    int first, count;
    bool started;
} reader;

static void push(const char *line)
{
    lock(&reader.lock);
    if (reader.count < STDIN_LINES) {
        snprintf(reader.lines[(reader.first + reader.count) % STDIN_LINES], STDIN_LINE_SIZE, "%s", line);
        reader.count++;
    }
    unlock(&reader.lock);
}

static void read_lines(void)
{
    char line[STDIN_LINE_SIZE];
    while (fgets(line, sizeof line, stdin)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        push(line);
    }
}

#ifdef _WIN32
static DWORD WINAPI thread_main(LPVOID arg)
{
    (void)arg;
    read_lines();
    return 0;
}
#else
static void *thread_main(void *arg)
{
    (void)arg;
    read_lines();
    return NULL;
}
#endif

bool stdin_reader_start(void)
{
    if (reader.started) return true;
    lock_init(&reader.lock);
#ifdef _WIN32
    HANDLE h = CreateThread(NULL, 0, thread_main, NULL, 0, NULL);
    if (!h) return false;
    CloseHandle(h);
#else
    pthread_t t;
    if (pthread_create(&t, NULL, thread_main, NULL) != 0) return false;
    pthread_detach(t);
#endif
    reader.started = true;
    return true;
}

bool stdin_reader_take(char *line, size_t size)
{
    if (!reader.started) return false;
    bool got = false;
    lock(&reader.lock);
    if (reader.count > 0) {
        snprintf(line, size, "%s", reader.lines[reader.first]);
        reader.first = (reader.first + 1) % STDIN_LINES;
        reader.count--;
        got = true;
    }
    unlock(&reader.lock);
    return got;
}
