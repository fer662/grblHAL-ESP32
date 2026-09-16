/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "storage.h"
#include "bridge.h"
#include "freertos/FreeRTOS.h"
#include "critical.h"
#include "grbl/planner.h"
#include "grbl/nvs_buffer.h"
#include "grbl/state_machine.h"
#include "grbl/stepper.h"
#include "nvs_flash.h"
#include "preferences.h"
#include "serial.h"
#include "update.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static nvs_handle_t handle;
static bool ready, valid, dirty;
static uint32_t changed_at, writes, failures;
static h5_preferences_t preferences;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static bool valid_preferences(const h5_preferences_t *p)
{
    return p->version == 1 && p->mode >= 0 && p->mode <= 8 && p->mode != 1 && p->measure >= 0 &&
           p->measure <= 2 && p->pitch_type >= 0 && p->pitch_type <= 2 && abs(p->pitch) <= 10000000 &&
           p->move_step >= 0 /* zero selects hold-to-run Rapids */ && p->move_step <= 10000000 && p->passes > 0 && p->passes <= 999 &&
           p->starts > 0 && p->starts <= 124 && isfinite(p->cone_ratio) && fabsf(p->cone_ratio) <= 10000 &&
           p->aux_forward <= 1 && p->sound <= 1 && p->jog_mode <= 1;
}
bool h5_storage_ready(void)
{
    return ready;
}
void h5_storage_init(void)
{
    // A separate partition preserves every byte of the original H5 NVS.
    // Never erase on an init error: report it and retain RAM-only operation.
    if (nvs_flash_init_partition("h5_settings") != ESP_OK ||
        nvs_open_from_partition("h5_settings", "controller", NVS_READWRITE, &handle) != ESP_OK)
        return;
    ready = true;
    size_t size = sizeof(preferences);
    valid = nvs_get_blob(handle, "ui_v1", &preferences, &size) == ESP_OK && size == sizeof(preferences) &&
            valid_preferences(&preferences);
}
bool h5_preferences_get(h5_preferences_t *p)
{
    h5_critical_enter(&lock, 5000 + __LINE__);
    bool ok = valid;
    if (ok)
        *p = preferences;
    h5_critical_exit(&lock);
    return ok;
}
void h5_preferences_set(const h5_preferences_t *p)
{
    if (!valid_preferences(p))
        return;
    h5_critical_enter(&lock, 5000 + __LINE__);
    if (!valid || memcmp(p, &preferences, sizeof(*p))) {
        preferences = *p;
        valid = dirty = true;
        changed_at = xTaskGetTickCount();
    }
    h5_critical_exit(&lock);
}
static bool flash_idle(void)
{
    return !h5_update_active() &&
           (!sys.driver_started || (state_get() == STATE_IDLE && h5_motion_idle() && !st_is_stepping() &&
                                    !plan_get_current_block() && !h5_cycle_busy()));
}
static bool read_core(uint8_t *data)
{
    memset(data, 0xff, hal.nvs.size);
    size_t size = hal.nvs.size;
    return ready && nvs_get_blob(handle, "grbl_v23", data, &size) == ESP_OK && size == hal.nvs.size;
}
static bool write_core(uint8_t *data)
{
    if (!ready || !flash_idle())
        return false;
    bool ok = nvs_set_blob(handle, "grbl_v23", data, hal.nvs.size) == ESP_OK && nvs_commit(handle) == ESP_OK;
    if (ok)
        writes++;
    else
        failures++;
    return ok;
}
void h5_storage_hal(void)
{
    hal.nvs.type = ready ? NVS_Flash : NVS_None;
    if (ready) {
        hal.nvs.memcpy_from_flash = read_core;
        hal.nvs.memcpy_to_flash = write_core;
    }
}
void h5_storage_upgrade_motion(void)
{
    // Boot-only native settings upgrades. Preserve explicit tuning and bench profiles.
    if (!ready || H5_BENCH_ONLY) return;
    uint8_t revision = 0;
    esp_err_t result = nvs_get_u8(handle, "motion_rev", &revision);
    if ((result != ESP_OK && result != ESP_ERR_NVS_NOT_FOUND) || revision >= 3) return;
    uint32_t before = writes;
    bool changed = false;
    if (revision < 1 && settings.axis[Z_AXIS].acceleration == 50.0f * 3600.0f) {
        char value[] = "100";
        if (settings_store_setting(Setting_AxisAcceleration + Z_AXIS, value) != Status_OK) return;
        changed = true;
    }
    if (revision < 2 && settings.axis[X_AXIS].max_rate == 60.0f) {
        char value[] = "300";
        if (settings_store_setting(Setting_AxisMaxRate + X_AXIS, value) != Status_OK) return;
        changed = true;
    }
    if (settings.axis[X_AXIS].acceleration == 25.0f * 3600.0f) {
        char value[] = "500";
        if (settings_store_setting(Setting_AxisAcceleration + X_AXIS, value) != Status_OK) return;
        changed = true;
    }
    if (changed) {
        nvs_buffer_sync_physical();
        // Do not mark the upgrade complete if the core blob did not reach flash.
        if (writes == before) return;
    }
    if (nvs_set_u8(handle, "motion_rev", 3) != ESP_OK || nvs_commit(handle) != ESP_OK)
        failures++;
}
void h5_storage_poll(void)
{
    if (!ready || !flash_idle() || !h5_bridge_empty() || h5_serial_pending())
        return;
    h5_preferences_t p;
    uint32_t generation;
    h5_critical_enter(&lock, 5000 + __LINE__);
    bool save = dirty && xTaskGetTickCount() - changed_at > pdMS_TO_TICKS(2000);
    p = preferences;
    generation = changed_at;
    h5_critical_exit(&lock);
    if (!save)
        return;
    bool ok = nvs_set_blob(handle, "ui_v1", &p, sizeof(p)) == ESP_OK && nvs_commit(handle) == ESP_OK;
    if (ok) {
        writes++;
        h5_critical_enter(&lock, 5000 + __LINE__);
        if (changed_at == generation)
            dirty = false;
        h5_critical_exit(&lock);
    } else
        failures++;
}
status_code_t h5_storage_command(sys_state_t state, char *line)
{
    if (strcmp(line, "P4STORE"))
        return Status_Unhandled;
    char text[180];
    snprintf(text, sizeof(text),
             "[P4STORE:READY:%u|DIRTY:%u|WRITES:%lu|FAILURES:%lu|UI_VERSION:%lu|PITCH:%ld|PASSES:%ld|STARTS:%"
             "ld]\r\n",
             ready, dirty, (unsigned long)writes, (unsigned long)failures, (unsigned long)preferences.version,
             (long)preferences.pitch, (long)preferences.passes, (long)preferences.starts);
    hal.stream.write(text);
    return Status_OK;
}
bool h5_storage_wifi(char ssid[33], char password[65])
{
    size_t a = 33, b = 65;
    return ready && nvs_get_str(handle, "ssid", ssid, &a) == ESP_OK &&
           nvs_get_str(handle, "wifi_password", password, &b) == ESP_OK;
}
bool h5_storage_set_wifi(const char *ssid, const char *password)
{
    return ready && flash_idle() && nvs_set_str(handle, "ssid", ssid) == ESP_OK &&
           nvs_set_str(handle, "wifi_password", password) == ESP_OK && nvs_commit(handle) == ESP_OK;
}
bool h5_storage_reject_next_ota(bool reject)
{
    return ready && flash_idle() && nvs_set_u8(handle, "reject_next_ota", reject) == ESP_OK &&
           nvs_commit(handle) == ESP_OK;
}
bool h5_storage_consume_ota_rejection(void)
{
    uint8_t reject = 0;
    if (!ready || nvs_get_u8(handle, "reject_next_ota", &reject) != ESP_OK || !reject)
        return false;
    nvs_set_u8(handle, "reject_next_ota", 0);
    nvs_commit(handle);
    return true;
}
