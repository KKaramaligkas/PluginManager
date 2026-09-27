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
} input_state;

extern unsigned int BTN_CONFIRM;
extern unsigned int BTN_CANCEL;

void input_init(void);
void input_update(input_state *in);
/* Waits until every button is released (used before exiting dialogs). */
void input_flush(void);

#endif
