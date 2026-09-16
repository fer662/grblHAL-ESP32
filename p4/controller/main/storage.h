#pragma once
#include "grbl/hal.h"
void h5_storage_init(void);
void h5_storage_hal(void);
void h5_storage_upgrade_motion(void); // boot only, after settings/hardware initialization
void h5_storage_poll(void);
bool h5_storage_restore_position(int32_t (*position)[N_AXIS]);
bool h5_storage_ready(void);
status_code_t h5_storage_command(sys_state_t,char *);
bool h5_storage_wifi(char ssid[33],char password[65]);
bool h5_storage_set_wifi(const char *ssid,const char *password);
bool h5_storage_reject_next_ota(bool reject);
bool h5_storage_consume_ota_rejection(void); // boot only, before controller tasks
