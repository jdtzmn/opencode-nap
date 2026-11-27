#include "wake-screen.h"

#include <string.h>

#define SYNC_ON "\033[?2026h"
#define SYNC_OFF "\033[?2026l"
#define SYNC_QUERY "\033[?2026$p"
#define SYNC_REPLY "\033[?2026;2$y"
#define ENTER_ALT "\033[?1049h"
#define LEAVE_ALT "\033[?1049l"
#define CLEAR_MAIN "\033[0m\033[2J\033[H"

#define emit(screen, literal) ((screen)->write((screen)->ctx, literal, sizeof(literal) - 1))
#define is_control(screen, literal) \
    ((screen)->length == sizeof(literal) - 1 && memcmp((screen)->control, literal, sizeof(literal) - 1) == 0)

void wake_input(WakeScreen *screen, const char *data, size_t size)
{
    if (!screen->queried || screen->supported || screen->done || screen->deadline) return;
    for (size_t i = 0; i < size; i++) {
        if (data[i] == SYNC_REPLY[screen->reply]) {
            if (++screen->reply == sizeof(SYNC_REPLY) - 1) {
                screen->supported = true;
                return;
            }
        } else {
            screen->reply = data[i] == '\033' ? 1 : 0;
        }
    }
}

void wake_finish(WakeScreen *screen)
{
    if (screen->deadline) emit(screen, SYNC_OFF);
    if (screen->length) screen->write(screen->ctx, screen->control, screen->length);
    screen->length = 0;
    screen->deadline = 0;
    screen->done = true;
}

// Buffer only incomplete control sequences. In particular, OSC and DCS text
// is not visible even when delivered across several PTY reads.
static bool control_complete(const WakeScreen *screen)
{
    if (screen->length < 2) return false;
    unsigned char last = screen->control[screen->length - 1];
    if (last == 0x18 || last == 0x1a) return true; // CAN, SUB
    switch (screen->control[1]) {
    case '[':
        return screen->length > 2 && last >= '@' && last <= '~';
    case ']':
        if (last == '\a') return true;
        /* fall through */
    case 'P': case 'X': case '^': case '_':
        return screen->length > 3 && last == '\\' && screen->control[screen->length - 2] == '\033';
    default:
        return last >= '0' && last <= '~';
    }
}

static void forward_control(WakeScreen *screen, int64_t now)
{
    bool suppress = false;
    if (is_control(screen, SYNC_QUERY)) {
        screen->queried = true;
    } else if (!screen->deadline && is_control(screen, ENTER_ALT)) {
        if (screen->supported) {
            emit(screen, SYNC_ON);
            screen->deadline = now + 750;
        } else {
            screen->done = true;
        }
        emit(screen, CLEAR_MAIN);
    } else if (screen->deadline && is_control(screen, SYNC_ON)) {
        screen->frame = true;
        screen->text = false;
        suppress = true;
    } else if (screen->deadline && is_control(screen, SYNC_OFF)) {
        bool visible = screen->frame && screen->text;
        screen->frame = false;
        if (visible) {
            screen->length = 0;
            wake_finish(screen);
        }
        suppress = true;
    } else if (screen->deadline && is_control(screen, LEAVE_ALT)) {
        wake_finish(screen);
        return;
    }
    if (!suppress) screen->write(screen->ctx, screen->control, screen->length);
    screen->length = 0;
}

void wake_output(WakeScreen *screen, const char *data, size_t size, int64_t now)
{
    size_t offset = 0;
    while (!screen->done && offset < size) {
        if (screen->length || data[offset] == '\033') {
            screen->control[screen->length++] = data[offset++];
            if (control_complete(screen)) forward_control(screen, now);
            else if (screen->length == sizeof(screen->control)) wake_finish(screen);
            continue;
        }

        size_t start = offset;
        do {
            if (screen->frame && (unsigned char)data[offset] > ' ' && data[offset] != 0x7f)
                screen->text = true;
            offset++;
        } while (offset < size && data[offset] != '\033');
        screen->write(screen->ctx, data + start, offset - start);
    }
    if (offset < size) screen->write(screen->ctx, data + offset, size - offset);
}
