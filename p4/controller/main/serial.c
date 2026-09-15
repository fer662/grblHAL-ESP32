/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "grbl/hal.h"
#include "grbl/protocol.h"
#include "grbl/stepper.h"
#include "serial.h"
#include "bridge.h"
#include "cycle.h"
#include "grbl/state_machine.h"
static bool usb_line_active, cycle_dropping;
static char cycle_line[64];
static unsigned cycle_line_length;
bool h5_serial_pending(void) { return usb_line_active || hal.stream.get_rx_buffer_count(); }

// Only the grbl task reads UART and owns this ring. Realtime commands are
// extracted before queued G-code, including while the planner buffer is full.
static uint8_t rx[1024];
static unsigned head, tail, overflows;
static bool dropping_line;
static QueueHandle_t uart_events;
extern void h5_motion_fault(void);
static enqueue_realtime_command_ptr realtime = protocol_enqueue_realtime_command;
static unsigned count(void) { return (head - tail) & (sizeof(rx) - 1); }
static uint16_t available(void) { return count(); }
static uint16_t space(void) { return sizeof(rx) - 1 - count(); }
static void flush(void)
{
    head = tail = 0;
    dropping_line = usb_line_active = cycle_dropping = false;
    cycle_line_length=0;
    h5_bridge_flush();
}
static void cancel(void) { flush(); rx[head++] = ASCII_CAN; }
static bool write_char(const uint8_t c)
{
    while (uart_tx_chars(UART_NUM_0, (const char *)&c, 1) == 0) {
        // Do not block the planner's foreground service on a full UART FIFO.
        if (!hal.stream_blocking_callback()) return false;
        if (st_is_stepping()) st_prep_buffer();
    }
    return true;
}
static void write_string(const char *s)
{
    while (*s && write_char((uint8_t)*s)) s++;
}
static enqueue_realtime_command_ptr set_realtime(enqueue_realtime_command_ptr fn)
{
    enqueue_realtime_command_ptr previous = realtime;
    if (fn) realtime = fn;
    return previous;
}
unsigned h5_serial_overflows(void) { return overflows; }
void h5_serial_poll(void)
{
    uart_event_t event;
    while (xQueueReceive(uart_events, &event, 0) == pdTRUE) {
        if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL ||
            event.type == UART_PARITY_ERR || event.type == UART_FRAME_ERR) {
            overflows++;
            uart_flush_input(UART_NUM_0);
            flush();
            h5_motion_fault();
        }
    }
    uint8_t data[64];
    int n = uart_read_bytes(UART_NUM_0, data, sizeof(data), 0);
    for (int i = 0; i < n; i++) {
        uint8_t c = data[i];
        if (h5_cycle_busy() && (c == CMD_JOG_CANCEL || c == CMD_FEED_HOLD || c == CMD_FEED_HOLD_LEGACY)) {
            h5_cycle_cancel(); continue;
        }
        if (h5_cycle_busy() && (c == CMD_CYCLE_START || c == CMD_CYCLE_START_LEGACY)) continue;
        if (realtime(c)) continue;
        // A cycle owns the parser. Reject competing USB lines immediately;
        // never save them to execute unexpectedly after a cycle finishes.
        if (h5_cycle_owns_stream() || cycle_dropping) {
            cycle_dropping = c != '\n' && c != '\r';
            if (cycle_dropping) {
                if (cycle_line_length < sizeof(cycle_line)-1) cycle_line[cycle_line_length++]=c;
            } else {
                cycle_line[cycle_line_length]=0;
                if (!strcmp(cycle_line,"$P4CYCLE")) {
                    h5_cycle_command(state_get(),cycle_line+1); write_string("ok\r\n");
                } else if (!strcmp(cycle_line,"$P4UITEST=8") && h5_ui_test_action('8')) write_string("ok\r\n");
                else if (cycle_line_length) write_string("error:8\r\n");
                cycle_line_length=0;
            }
            continue;
        }
        if (dropping_line) {
            if (c == '\n' || c == '\r') dropping_line = false;
            continue;
        }
        if (space()) {
            rx[head] = c;
            head = (head + 1) & (sizeof(rx) - 1);
        } else {
            // A truncated command must never become a valid but different move.
            overflows++;
            flush();
            dropping_line = true;
            h5_motion_fault();
        }
    }
}
static int32_t read_char(void)
{
    h5_serial_poll();
    if (h5_bridge_active() || (!usb_line_active && !count())) {
        int32_t c = h5_bridge_read();
        if (c != SERIAL_NO_DATA) return c;
    }
    if (!count()) return -1;
    uint8_t c = rx[tail];
    tail = (tail + 1) & (sizeof(rx) - 1);
    usb_line_active = c != '\n' && c != '\r' && c != ASCII_CAN;
    return c;
}
bool h5_serial_init(void)
{
    uart_config_t config = {
        .baud_rate = 115200, .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &config));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 4096, 0, 16, &uart_events, 0));
    ESP_ERROR_CHECK(uart_flush_input(UART_NUM_0));
    static const io_stream_t stream = {
        .type = StreamType_Serial, .instance = 0,
        .read = read_char, .write = write_string, .write_all = write_string,
        .write_char = write_char, .get_rx_buffer_count = available,
        .get_rx_buffer_free = space, .reset_read_buffer = flush,
        .cancel_read_buffer = cancel, .set_enqueue_rt_handler = set_realtime,
    };
    hal.rx_buffer_size = sizeof(rx);
    return stream_connect(&stream);
}
