/*
    Plugin Manager for ARK-5
    input.c: buttons with auto-repeat and the analog stick mapped to the d-pad.
*/

#include <string.h>

#include <pspkernel.h>
#include <psputility.h>

#include "entropy.h"
#include "input.h"

#define REPEAT_DELAY    (350 * 1000)
#define REPEAT_RATE     (70 * 1000)
#define ANALOG_DEAD     48

#define DIRS (PSP_CTRL_UP | PSP_CTRL_DOWN | PSP_CTRL_LEFT | PSP_CTRL_RIGHT)
#define REPEATABLE (DIRS | PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)

unsigned int BTN_CONFIRM = PSP_CTRL_CROSS;
unsigned int BTN_CANCEL = PSP_CTRL_CIRCLE;

static unsigned int last;
static unsigned int next_repeat;

#ifdef PM_AUTOTEST
/* Test builds only: "ms0:/pm_autotest.txt" lists "<frame> <BUTTON>" lines that
   are replayed as button presses, so the UI can be driven in an emulator. */
#include <stdio.h>
#include <stdlib.h>
#include "fs.h"
#include "util.h"

static struct { unsigned int frame, button; } script[64];
static int script_n, script_pos;
static unsigned int frame_no;

static void autotest_load(void)
{
    static const struct { const char *name; unsigned int bit; } names[] = {
        { "UP", PSP_CTRL_UP }, { "DOWN", PSP_CTRL_DOWN }, { "LEFT", PSP_CTRL_LEFT },
        { "RIGHT", PSP_CTRL_RIGHT }, { "CROSS", PSP_CTRL_CROSS }, { "CIRCLE", PSP_CTRL_CIRCLE },
        { "TRIANGLE", PSP_CTRL_TRIANGLE }, { "SQUARE", PSP_CTRL_SQUARE }, { "L", PSP_CTRL_LTRIGGER },
        { "R", PSP_CTRL_RTRIGGER }, { "START", PSP_CTRL_START }, { "SELECT", PSP_CTRL_SELECT },
    };
    char *text = fs_read_all("ms0:/pm_autotest.txt", NULL, 16 * 1024);
    if (!text) return;
    for (char *line = strtok(text, "\r\n"); line && script_n < 64; line = strtok(NULL, "\r\n")) {
        char name[16];
        unsigned int frame;
        if (sscanf(line, "%u %15s", &frame, name) != 2) continue;
        for (size_t i = 0; i < NELEMS(names); i++)
            if (!strcmp(name, names[i].name)) {
                script[script_n].frame = frame;
                script[script_n].button = names[i].bit;
                script_n++;
            }
    }
    free(text);
}
#endif

void input_init(void)
{
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    int swap = 1;   /* 1 = X confirms (default outside Japan) */
    if (sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_UNKNOWN, &swap) == 0 && swap == 0) {
        BTN_CONFIRM = PSP_CTRL_CIRCLE;
        BTN_CANCEL = PSP_CTRL_CROSS;
    }
    last = 0;
#ifdef PM_AUTOTEST
    autotest_load();
#endif
}

void input_update(input_state *in)
{
    SceCtrlData pad;
    memset(&pad, 0, sizeof(pad));
    sceCtrlPeekBufferPositive(&pad, 1);

    unsigned int buttons = pad.Buttons & 0x0000FFFF;
    int ax = (int)pad.Lx - 128, ay = (int)pad.Ly - 128;
    if (ax < -ANALOG_DEAD * 2) buttons |= PSP_CTRL_LEFT;
    if (ax > ANALOG_DEAD * 2) buttons |= PSP_CTRL_RIGHT;
    if (ay < -ANALOG_DEAD * 2) buttons |= PSP_CTRL_UP;
    if (ay > ANALOG_DEAD * 2) buttons |= PSP_CTRL_DOWN;
#ifdef PM_AUTOTEST
    frame_no++;
    while (script_pos < script_n && script[script_pos].frame < frame_no) script_pos++;
    if (script_pos < script_n && script[script_pos].frame == frame_no) buttons |= script[script_pos].button;
#endif

    /* input timing is a good entropy source for the TLS random generator */
    if (buttons != last) {
        unsigned int mix[2] = { buttons | ((unsigned)pad.Lx << 16) | ((unsigned)pad.Ly << 24), pad.TimeStamp };
        entropy_add(mix, sizeof(mix));
    }

    unsigned int now = sceKernelGetSystemTimeLow();
    in->held = buttons;
    in->pressed = buttons & ~last;
    in->repeat = in->pressed;

    if (buttons & REPEATABLE) {
        if (in->pressed & REPEATABLE) {
            next_repeat = now + REPEAT_DELAY;
        }
        else if ((int)(now - next_repeat) >= 0) {
            in->repeat |= buttons & REPEATABLE;
            next_repeat = now + REPEAT_RATE;
        }
    }
    last = buttons;
}

void input_flush(void)
{
    SceCtrlData pad;
    for (int i = 0; i < 100; i++) {
        sceCtrlPeekBufferPositive(&pad, 1);
        if (!(pad.Buttons & 0xFFFF)) break;
        sceKernelDelayThread(10000);
    }
    last = pad.Buttons & 0xFFFF;
}
