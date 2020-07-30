#ifndef _SEATD_TERMINAL_H
#define _SEATD_TERMINAL_H

#include <stdbool.h>

int terminal_setup(int vt);
int terminal_teardown(int vt);
int terminal_current_vt(void);
int terminal_switch_vt(int vt);
int terminal_ack_switch(void);
int terminal_set_keyboard(int vt, bool enable);
int terminal_set_graphics(int vt, bool enable);

#endif
