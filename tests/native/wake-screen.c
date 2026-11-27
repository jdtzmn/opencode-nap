#include "../../native/wake-screen.h"

#include <assert.h>
#include <string.h>

#define ON "\033[?2026h"
#define OFF "\033[?2026l"
#define QUERY "\033[?2026$p"
#define ALT "\033[?1049h"
#define CLEAR "\033[0m\033[2J\033[H"

typedef struct {
    char data[256];
    size_t length;
} Output;

static void append(void *ctx, const char *data, size_t size)
{
    Output *output = ctx;
    assert(output->length + size < sizeof(output->data));
    memcpy(output->data + output->length, data, size);
    output->length += size;
    output->data[output->length] = '\0';
}

static void send(WakeScreen *screen, const char *data)
{
    // Every read boundary, including a split escape or UTF-8 sequence.
    for (; *data; data++) wake_output(screen, data, 1, 100);
}

int main(void)
{
    Output output = {0};
    WakeScreen screen = {.write = append, .ctx = &output};
    send(&screen, QUERY);
    wake_input(&screen, "\033[?20", 5);
    wake_input(&screen, "26;2$y", 6);
    send(&screen, ALT);
    assert(screen.deadline == 850);
    send(&screen, ON "\033]0;window title\a\033[2J " OFF);
    assert(!screen.done);
    assert(strcmp(output.data, QUERY ON CLEAR ALT "\033]0;window title\a\033[2J ") == 0);
    send(&screen, ON "你好" OFF ON "more" OFF);
    assert(screen.done && screen.deadline == 0);
    assert(strcmp(output.data, QUERY ON CLEAR ALT "\033]0;window title\a\033[2J 你好" OFF ON "more" OFF) == 0);

    output = (Output){0};
    screen = (WakeScreen){.write = append, .ctx = &output};
    send(&screen, QUERY ALT ON "visible" OFF);
    assert(screen.done && screen.deadline == 0);
    assert(strcmp(output.data, QUERY CLEAR ALT ON "visible" OFF) == 0);

    output = (Output){0};
    screen = (WakeScreen){.write = append, .ctx = &output, .supported = true};
    send(&screen, ALT ON "\033[1;");
    wake_finish(&screen);
    send(&screen, "3H");
    assert(strcmp(output.data, ON CLEAR ALT OFF "\033[1;3H") == 0);
}
