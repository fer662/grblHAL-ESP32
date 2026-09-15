#pragma once
#include "grbl/hal.h"
void h5_network_start(void);
void h5_network_poll(void);
status_code_t h5_network_command(sys_state_t,char *);
