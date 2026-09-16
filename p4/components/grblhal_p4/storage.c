/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>
#include "nvs_flash.h"
#include "grbl/hal.h"
#include "grbl/planner.h"
#include "grbl/state_machine.h"
#include "grbl/stepper.h"
#include "p4_driver.h"
#include "storage.h"
static nvs_handle_t handle;
static bool ready;
bool p4_storage_ready(void) { return ready; }
void p4_storage_init(void)
{
    // Preserve existing storage on any initialization error; use RAM-only settings.
    ready = nvs_flash_init() == ESP_OK &&
            nvs_open("grblhal_p4", NVS_READWRITE, &handle) == ESP_OK;
}
static bool read_core(uint8_t *data)
{
    memset(data, 0xff, hal.nvs.size);
    size_t size = hal.nvs.size;
    return ready && nvs_get_blob(handle, "settings", data, &size) == ESP_OK && size == hal.nvs.size;
}
static bool write_core(uint8_t *data)
{
    if (!ready || (sys.driver_started && (state_get() != STATE_IDLE ||
        !p4_motion_idle() || st_is_stepping() || plan_get_current_block()))) return false;
    return nvs_set_blob(handle, "settings", data, hal.nvs.size) == ESP_OK && nvs_commit(handle) == ESP_OK;
}
void p4_storage_hal(void)
{
    hal.nvs.type = ready ? NVS_Flash : NVS_None;
    if (ready) {
        hal.nvs.memcpy_from_flash = read_core;
        hal.nvs.memcpy_to_flash = write_core;
    }
}
