/*
    Plugin Manager for ARK-5
    input.h: buttons with auto-repeat, analog stick as d-pad, confirm/cancel
    following the system "Enter button" setting.
*/

#ifndef PM_INPUT_H
#define PM_INPUT_H

#include <pspctrl.h>

typedef struct {
    unsigned int held;      /* currently down */
    unsigned int pressed;   /* went down this frame */
    unsigned int repeat;    /* pressed, or auto-repeated while held */
    int lx, ly;             /* analog stick, -128 to 127 */
} input_state;

extern unsigned int BTN_CONFIRM;
extern unsigned int BTN_CANCEL;

void input_init(void);
/* Whether the analog stick also presses the d-pad (on by default). */
void input_set_analog_dpad(int on);
void input_update(input_state *in);
#ifdef PM_AUTOTEST
/* Test builds: the replayed button script waits while `on` (a page loading). */
void input_autotest_hold(int on);
#endif
/* Waits until every button is released (used before exiting dialogs). */
void input_flush(void);

#endif
