/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "bridge.h"
#include "diagnostics_internal.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "grbl/hal.h"
#include "grbl/stepper.h"
#include "trinamic/tmc5160.h"
#include <stdio.h>
#include <string.h>
static spi_device_handle_t device;
static TMC5160_t driver;
static bool transport_ok = true, configured;
static uint8_t exchange(uint8_t address, uint32_t *value)
{
    spi_transaction_t t = {.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA, .length = 40};
    // A TMC datagram is five bytes, larger than IDF's four-byte inline buffer.
    uint8_t tx[5] = {address, *value >> 24, *value >> 16, *value >> 8, *value}, rx[5] = {0};
    t.flags = 0;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    if (spi_device_polling_transmit(device, &t) != ESP_OK)
        transport_ok = false;
    *value = ((uint32_t)rx[1] << 24) | ((uint32_t)rx[2] << 16) | ((uint32_t)rx[3] << 8) | rx[4];
    return rx[0];
}
TMC_spi_status_t tmc_spi_write(trinamic_motor_t motor, TMC_spi_datagram_t *d)
{
    uint32_t value = d->payload.value;
    return exchange(d->addr.idx | 0x80, &value);
}
TMC_spi_status_t tmc_spi_read(trinamic_motor_t motor, TMC_spi_datagram_t *d)
{
    uint32_t value = 0;
    exchange(d->addr.idx, &value);
    value = 0;
    uint8_t status = exchange(d->addr.idx, &value);
    d->payload.value = value;
    return status;
}
void h5_tmc_init(void)
{
    spi_bus_config_t bus = {.mosi_io_num = 48,
                            .miso_io_num = 50,
                            .sclk_io_num = 52,
                            .quadwp_io_num = -1,
                            .quadhd_io_num = -1,
                            .max_transfer_sz = 5};
    spi_device_interface_config_t cfg = {
        .clock_speed_hz = 1000000, .mode = 3, .spics_io_num = 51, .queue_size = 1, .cs_ena_posttrans = 2};
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED) != ESP_OK ||
        spi_bus_add_device(SPI2_HOST, &cfg, &device) != ESP_OK) {
        device = NULL;
        return;
    }
    gpio_pullup_en(50);
    TMC5160_SetDefaults(&driver);
    driver.config.current = 1700;
    driver.config.r_sense = 75;
    driver.config.microsteps = TMC5160_Microsteps_2;
    // STEP/DIR remains the sole motion source. Keep spreadCycle, with 256-step
    // interpolation, and preserve the existing 2-microstep calibration.
    driver.gconf.reg.en_pwm_mode = 0;
    driver.coolconf.reg.value = 0;
    TMC5160_ReadRegister(&driver, (TMC5160_datagram_t *)&driver.ioin);
    if (driver.ioin.reg.version != 0x30)
        return;
    configured = TMC5160_Init(&driver) && transport_ok;
}
status_code_t h5_tmc_command(sys_state_t state, char *line)
{
    if (strcmp(line, "P4TMC"))
        return Status_Unhandled;
    if (state != STATE_IDLE || st_is_stepping() || h5_cycle_busy())
        return Status_IdleError;
    if (device) {
        TMC5160_ReadRegister(&driver, (TMC5160_datagram_t *)&driver.ioin);
        TMC5160_ReadRegister(&driver, (TMC5160_datagram_t *)&driver.drv_status);
        TMC5160_ReadRegister(&driver, (TMC5160_datagram_t *)&driver.chopconf);
    }
    char report[250];
    snprintf(report, sizeof(report),
             "[P4TMC:SPI:%s|DEVICE:%s|CONFIGURED:%u|IOIN:%08lx|CHOPCONF:%08lx|DRV_STATUS:%08lx|CURRENT_MA:"
             "1700|MICROSTEPS:2|RSENSE_MOHM:75|MODE:SPREADCYCLE|EN:%s]\r\n",
             device && transport_ok ? "OK" : "ERROR",
             device && driver.ioin.reg.version == 0x30 ? "TMC5160" : "MISSING", configured,
             (unsigned long)driver.ioin.reg.value, (unsigned long)driver.chopconf.reg.value,
             (unsigned long)driver.drv_status.reg.value, H5_BENCH_ONLY?"LOCKED":"CONTROLLED");
    hal.stream.write(report);
    return Status_OK;
}

void h5_tmc_snapshot(h5_diagnostics_t *s, bool refresh)
{
    // Sole grbl task; caller only refreshes when the complete motion path is idle.
    if (refresh && device) {
        TMC5160_ReadRegister(&driver, (TMC5160_datagram_t *)&driver.ioin);
        TMC5160_ReadRegister(&driver, (TMC5160_datagram_t *)&driver.drv_status);
        TMC5160_ReadRegister(&driver, (TMC5160_datagram_t *)&driver.chopconf);
    }
    s->tmc_transport = device && transport_ok;
    s->tmc_present = device && driver.ioin.reg.version == 0x30;
    s->tmc_configured = configured;
    s->tmc_ioin = driver.ioin.reg.value;
    s->tmc_chopconf = driver.chopconf.reg.value;
    s->tmc_status = driver.drv_status.reg.value;
}
