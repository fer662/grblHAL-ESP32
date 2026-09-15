/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "diagnostics_internal.h"
#include "bridge.h"
#include "update.h"
#include "critical.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static h5_diagnostics_t published;
static atomic_uint expires;
static uint32_t now_ms(void) { return esp_timer_get_time() / 1000; }
void h5_diagnostics_start(void) { atomic_store(&expires, now_ms() + 1800000); }
void h5_diagnostics_stop(void) { atomic_store(&expires, 0); }
bool h5_diagnostics_active(void)
{
    unsigned until = atomic_load(&expires);
    if (!until) return false;
    if ((int32_t)(until - now_ms()) > 0) return true;
    atomic_compare_exchange_strong(&expires, &until, 0);
    return false;
}
void h5_diagnostics_snapshot(h5_diagnostics_t *s)
{
    h5_critical_enter(&lock, 6000 + __LINE__);
    *s = published;
    h5_critical_exit(&lock);
}
void h5_diagnostics_poll(bool idle)
{
    static uint32_t last_sample, last_tmc;
    uint32_t now = now_ms();
    if (!h5_diagnostics_active() || now - last_sample < 250) return;
    last_sample = now;
    h5_diagnostics_t s = { .sampled_ms = now };
    h5_driver_snapshot(&s);
    h5_spindle_snapshot(&s);
    bool refresh = idle && now - last_tmc >= 2000;
    if (refresh) last_tmc = now;
    h5_tmc_snapshot(&s, refresh);
    s.tmc_sampled_ms = last_tmc;
    h5_critical_snapshot(s.critical);
    h5_critical_enter(&lock, 6000 + __LINE__);
    published = s;
    h5_critical_exit(&lock);
}
static bool send_all(int fd, const char *text, size_t length)
{
    while (length) {
        int n = send(fd, text, length, 0);
        if (n <= 0) return false;
        text += n; length -= n;
    }
    return true;
}
static void reply(int fd, const char *status, const char *type, const char *body)
{
    char header[240];
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\n\r\n",
        status, type, (unsigned)strlen(body));
    if (send_all(fd, header, n)) send_all(fd, body, strlen(body));
}
void h5_diagnostics_serve(int fd)
{
    if (!h5_diagnostics_active()) {
        reply(fd, "403 Forbidden", "text/plain", "Open Diagnostics on the tablet to start a 30-minute session.\n");
        return;
    }
    // A bounded HTTP reader, with socket deadlines set by the network owner.
    // No request is ever forwarded to the motion parser or register writer.
    char request[512] = {0};
    size_t used = 0;
    uint32_t began = now_ms();
    while (!strstr(request, "\r\n\r\n") && used < sizeof(request) - 1) {
        int n = recv(fd, request + used, sizeof(request) - 1 - used, 0);
        if (n <= 0) return;
        if (now_ms() - began > 1000) return;
        used += n; request[used] = 0;
        if (memchr(request, 0, used)) break;
    }
    if (!strstr(request, "\r\n\r\n")) {
        reply(fd, "400 Bad Request", "text/plain", "Invalid or oversized request.\n"); return;
    }
    if (strncmp(request, "GET ", 4)) {
        reply(fd, "405 Method Not Allowed", "text/plain", "Read-only diagnostics.\n"); return;
    }
    if (strncmp(request, "GET /diagnostics HTTP/1.1\r\n", 27) &&
        strncmp(request, "GET /diagnostics HTTP/1.0\r\n", 27)) {
        reply(fd, "404 Not Found", "text/plain", "Use /diagnostics.\n"); return;
    }
    if (!h5_diagnostics_active()) {
        reply(fd, "403 Forbidden", "text/plain", "Diagnostics session ended.\n"); return;
    }
    h5_diagnostics_t s; h5_diagnostics_snapshot(&s);
    h5_status_t state; h5_bridge_snapshot(&state);
    h5_update_status_t update; h5_update_snapshot(&update);
    char body[2300];
    snprintf(body, sizeof(body),
        "{\n  \"version\":\"%s\",\"partition\":\"%s\",\"uptime_ms\":%lu,\"sample_age_ms\":%lu,"
        "\"ready\":%u,\"state\":\"%s\",\"alarm\":%d,\n"
        "  \"ota_pairing_required\":%s,\"ota_validation_pending\":%s,\n"
        "  \"motor_enables_locked\":%s,\"enable_pins\":[%u,%u],\"position_steps_xz\":[%ld,%ld],\n"
        "  \"encoder_counts\":%lld,\"encoder_cpr\":1200,\"rpm\":%.3f,\"simulated\":%u,"
        "\"tracking\":%u,\"waiting_index\":%u,\"sync_fault\":\"%s\",\n"
        "  \"issued_steps_xz\":[%lu,%lu],\"counted_steps_xz\":[%ld,%ld],"
        "\"fault\":%u,\"late\":%lu,\"overlap\":%lu,\n"
        "  \"max_isr_us\":%lu,\"pulse_ticks_10mhz\":[%lu,%lu],"
        "\"deadline\":{\"kind\":%lu,\"elapsed\":%lu,\"period\":%lu,\"counter\":%lu},\n"
        "  \"critical_body_us\":[%u,%u],\"critical_site\":[%u,%u],\n"
        "  \"tmc\":{\"sampled_ms\":%lu,\"transport_ok\":%u,\"present\":%u,\"configured\":%u,"
        "\"ioin\":\"%08lx\",\"chopconf\":\"%08lx\",\"drv_status\":\"%08lx\","
        "\"current_ma\":1700,\"microsteps\":2,\"rsense_mohm\":75}\n}\n",
        esp_app_get_description()->version, esp_ota_get_running_partition()->label,
        (unsigned long)now_ms(), (unsigned long)(now_ms() - s.sampled_ms), state.ready, state.state, state.alarm,
        update.pairing_required?"true":"false", update.validation_pending?"true":"false",
        h5_motor_controls_enabled()?"false":"true", s.enable_x, s.enable_z, (long)state.position[0], (long)state.position[2],
        (long long)s.encoder, (double)s.rpm, s.simulated, s.tracking, s.waiting, s.sync_fault,
        (unsigned long)s.x_pulses, (unsigned long)s.z_pulses, (long)s.x_counted, (long)s.z_counted,
        s.fault, (unsigned long)s.late, (unsigned long)s.overlap, (unsigned long)s.isr_us,
        (unsigned long)s.pulse_min, (unsigned long)s.pulse_max,
        (unsigned long)s.deadline_kind, (unsigned long)s.deadline_elapsed,
        (unsigned long)s.deadline_period, (unsigned long)s.deadline_counter,
        s.critical[0] >> 16, s.critical[1] >> 16, s.critical[0] & 65535, s.critical[1] & 65535,
        (unsigned long)s.tmc_sampled_ms, s.tmc_transport, s.tmc_present, s.tmc_configured,
        (unsigned long)s.tmc_ioin, (unsigned long)s.tmc_chopconf, (unsigned long)s.tmc_status);
    reply(fd, "200 OK", "application/json", body);
}
