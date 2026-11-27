#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "wake-screen.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif

#define CLEAR "\033[0m\033[2J\033[H"
#define CHUNK 8192

typedef struct {
    char data[CHUNK * 4];
    size_t start;
    size_t end;
} Buffer;

static const int signals[] = {SIGCHLD, SIGWINCH, SIGHUP, SIGTERM, SIGINT, SIGQUIT, SIGTSTP, SIGCONT};
static int signal_pipe[2];

static void notify(int sig)
{
    int saved_errno = errno;
    unsigned char byte = sig;
    ssize_t ignored = write(signal_pipe[1], &byte, 1);
    (void)ignored;
    errno = saved_errno;
}

static void set_signals(void (*handler)(int))
{
    struct sigaction action = {.sa_handler = handler};
    sigemptyset(&action.sa_mask);
    for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); i++)
        sigaction(signals[i], &action, NULL);
}

static int64_t milliseconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * INT64_C(1000) + now.tv_nsec / 1000000;
}

static bool prepare_fd(int fd)
{
    return fcntl(fd, F_SETFD, FD_CLOEXEC) == 0 && fcntl(fd, F_SETFL, O_NONBLOCK) == 0;
}

static void append(void *ctx, const char *data, size_t size)
{
    Buffer *buffer = ctx;
    memcpy(buffer->data + buffer->end, data, size);
    buffer->end += size;
}

static bool flush(int fd, Buffer *buffer)
{
    ssize_t size = write(fd, buffer->data + buffer->start, buffer->end - buffer->start);
    if (size < 0) return errno == EINTR || errno == EAGAIN;
    buffer->start += size;
    if (buffer->start == buffer->end) *buffer = (Buffer){0};
    return true;
}

static void resize(int tty, int master)
{
    struct winsize size;
    if (ioctl(tty, TIOCGWINSZ, &size) == 0) ioctl(master, TIOCSWINSZ, &size);
}

static void send_signal(pid_t child, int sig)
{
    // The child may not have called setsid yet.
    if (kill(-child, sig) < 0 && errno == ESRCH) kill(child, sig);
}

static int direct(char **command)
{
    ssize_t ignored = write(STDOUT_FILENO, CLEAR, sizeof(CLEAR) - 1);
    (void)ignored;
    execv(command[0], command);
    perror(command[0]);
    return 127;
}

static void suspend(int tty, const struct termios *saved, pid_t child)
{
    send_signal(child, SIGSTOP);
    tcsetattr(tty, TCSANOW, saved);
    raise(SIGSTOP);
}

static void forward_signals(int tty, int master, const struct termios *saved,
    const struct termios *raw, pid_t child)
{
    unsigned char pending[64];
    ssize_t count = read(signal_pipe[0], pending, sizeof(pending));
    for (ssize_t i = 0; i < count; i++) {
        switch (pending[i]) {
        case SIGCHLD:
            break;
        case SIGWINCH:
            resize(tty, master);
            break;
        case SIGTSTP:
            suspend(tty, saved, child);
            break;
        case SIGCONT:
            tcsetattr(tty, TCSANOW, raw);
            resize(tty, master);
            send_signal(child, SIGCONT);
            break;
        default:
            send_signal(child, pending[i]);
        }
    }
}

static bool forward_output(int master, WakeScreen *screen)
{
    char data[CHUNK];
    ssize_t size = read(master, data, sizeof(data));
    if (size > 0) wake_output(screen, data, size, milliseconds());
    return size > 0 || (size < 0 && (errno == EINTR || errno == EAGAIN));
}

static int relay(int tty, int master, pid_t child, const struct termios *saved, const struct termios *raw)
{
    Buffer output = {0}, input = {0};
    WakeScreen screen = {.write = append, .ctx = &output};
    int status = 0;
    bool exited = false, eof = false;

    while (!eof || output.end || !exited) {
        if (!exited) {
            pid_t result = waitpid(child, &status, WNOHANG | WUNTRACED);
            if (result == child) {
                if (WIFSTOPPED(status)) suspend(tty, saved, child);
                else exited = true;
            }
        }
        int64_t now = milliseconds();
        if (screen.deadline && now >= screen.deadline) wake_finish(&screen);
        int timeout = screen.deadline ? (int)(screen.deadline - now) : -1;
        if (exited && !eof && !output.end) timeout = 0;
        struct pollfd fds[] = {
            {signal_pipe[0], POLLIN, 0},
            {tty, (output.end ? POLLOUT : 0) | (!input.end && !eof && !exited ? POLLIN : 0), 0},
            {eof ? -1 : master, (!output.end ? POLLIN : 0) | (input.end && !exited ? POLLOUT : 0), 0},
        };
        if (poll(fds, sizeof(fds) / sizeof(fds[0]), timeout) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (fds[0].revents && !exited) forward_signals(tty, master, saved, raw, child);
        if (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) break;
        if (fds[1].revents & POLLOUT && !flush(tty, &output)) break;
        if (fds[2].revents & POLLOUT && !flush(master, &input)) input = (Buffer){0};
        if (fds[1].revents & POLLIN) {
            ssize_t size = read(tty, input.data, CHUNK);
            if (size > 0) {
                input.end = size;
                wake_input(&screen, input.data, input.end);
            }
        }
        if (!eof && !output.end) {
            if (fds[2].revents & (POLLIN | POLLHUP | POLLERR)) eof = !forward_output(master, &screen);
            else if (exited) eof = true;
            if (eof) wake_finish(&screen);
        }
    }
    if (screen.deadline) {
        // On a broken connection, try to release the terminal without blocking.
        ssize_t ignored = write(tty, "\033[?2026l", sizeof("\033[?2026l") - 1);
        (void)ignored;
    }
    tcsetattr(tty, TCSANOW, saved);
    if (!exited) {
        send_signal(child, SIGHUP);
        return 1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

int main(int argc, char **argv)
{
    if (argc < 3 || strcmp(argv[1], "--wake") != 0) return 2;
    char **command = argv + 2;
    struct termios saved, raw;
    struct winsize size;
    // macOS poll() reports POLLNVAL for /dev/tty, so open the terminal's own device.
    const char *name = ttyname(STDIN_FILENO);
    int tty = name ? open(name, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC) : -1;
    if (tty < 0 || tcgetattr(tty, &saved) < 0 || ioctl(tty, TIOCGWINSZ, &size) < 0)
        return direct(command);
    if (pipe(signal_pipe) < 0 || !prepare_fd(signal_pipe[0]) || !prepare_fd(signal_pipe[1]))
        return direct(command);

    raw = saved;
    cfmakeraw(&raw);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    set_signals(notify);
    if (tcsetattr(tty, TCSANOW, &raw) < 0) {
        set_signals(SIG_DFL);
        return direct(command);
    }
    int master;
    pid_t child = forkpty(&master, NULL, &saved, &size);
    if (child == 0) {
        set_signals(SIG_DFL);
        execv(command[0], command);
        _exit(direct(command));
    }
    if (child < 0) {
        tcsetattr(tty, TCSANOW, &saved);
        set_signals(SIG_DFL);
        return direct(command);
    }
    if (!prepare_fd(master)) {
        tcsetattr(tty, TCSANOW, &saved);
        send_signal(child, SIGHUP);
        return 1;
    }
    return relay(tty, master, child, &saved, &raw);
}
