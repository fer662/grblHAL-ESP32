/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { bool active,connected,pairing_required,validation_pending;char ip[20],key[33],message[96];unsigned percent; } h5_update_status_t;
bool h5_update_request(void);
void h5_update_cancel(void);
bool h5_update_active(void);
bool h5_network_initialized(void);
void h5_update_snapshot(h5_update_status_t *);
#ifdef __cplusplus
}
#endif
