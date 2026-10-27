#pragma once

/* Replies from the PSV to a session: presets, typed text, 待った!. Kept free of
 * other project headers so model.h and protocol.h can both include it. */

typedef enum { RK_TEXT, RK_STOP, RK_CANCEL } ReplyKind;
