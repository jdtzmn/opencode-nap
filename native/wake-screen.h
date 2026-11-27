#ifndef NAP_WAKE_SCREEN_H
#define NAP_WAKE_SCREEN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*WakeWrite)(void *ctx, const char *data, size_t size);

typedef struct {
    WakeWrite write;
    void *ctx;
    int64_t deadline;
    bool queried;
    bool supported;
    bool done;
    bool frame;
    bool text;
    size_t reply;
    size_t length;
    char control[4096];
} WakeScreen;

void wake_input(WakeScreen *screen, const char *data, size_t size);
void wake_output(WakeScreen *screen, const char *data, size_t size, int64_t now);
void wake_finish(WakeScreen *screen);

#endif
