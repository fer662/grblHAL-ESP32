/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "network.h"
#include "diagnostics_internal.h"
#include "bridge.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "critical.h"
#include "freertos/task.h"
#include "grbl/planner.h"
#include "grbl/state_machine.h"
#include "grbl/stepper.h"
#include "lwip/sockets.h"
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"
#include "serial.h"
#include "storage.h"
#include "update.h"
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static h5_update_status_t published;
static atomic_bool requested, enabled, uploading, reconfigure, initialized;
bool h5_network_initialized(void) { return atomic_load(&initialized); }
static atomic_bool validation_pending;
static uint32_t validation_at, enabled_at;
static bool reject_validation;
static uint8_t key[16];
static uint32_t update_generation;
static char wifi_ssid[33], wifi_password[65];
static void message(const char *text)
{
    char next[sizeof(published.message)] = {0};
    snprintf(next, sizeof(next), "%s", text);
    h5_critical_enter(&lock, 4000 + __LINE__);
    memcpy(published.message, next, sizeof(next));
    h5_critical_exit(&lock);
}
bool h5_update_active(void) { return atomic_load(&enabled) || validation_pending; }
bool h5_update_request(void)
{
    atomic_store(&requested, true);
    return true;
}
void h5_update_cancel(void)
{
    h5_critical_enter(&lock, 4000 + __LINE__);
    atomic_store(&requested, false);
    if (!atomic_load(&uploading)) {
        atomic_store(&enabled, false);
        if (!validation_pending)
            h5_operation_release(H5_OWNER_UPDATE);
    }
    h5_critical_exit(&lock);
}
void h5_update_snapshot(h5_update_status_t *s)
{
    h5_critical_enter(&lock, 4000 + __LINE__);
    *s = published;
    h5_critical_exit(&lock);
    s->active = h5_update_active();
    s->pairing_required = H5_OTA_REQUIRE_PAIRING;
    s->validation_pending = atomic_load(&validation_pending);
}
static void hex(char *out, const uint8_t *in, size_t count)
{
    for (size_t i = 0; i < count; i++)
        sprintf(out + 2 * i, "%02x", in[i]);
}
static bool unhex(uint8_t *out, const char *in, size_t count)
{
    if (strlen(in) != count * 2)
        return false;
    for (size_t i = 0; i < count; i++) {
        unsigned n;
        char pair[3] = {in[2 * i], in[2 * i + 1], 0};
        if (strspn(pair, "0123456789abcdefABCDEF") != 2 || sscanf(pair, "%x", &n) != 1)
            return false;
        out[i] = n;
    }
    return true;
}
static bool equal(const uint8_t *a, const uint8_t *b, size_t n)
{
    unsigned difference = 0;
    for (size_t i = 0; i < n; i++)
        difference |= a[i] ^ b[i];
    return !difference;
}
static bool receive(int fd, void *buffer, size_t size)
{
    uint8_t *p = buffer;
    while (size && atomic_load(&enabled)) {
        int n = recv(fd, p, size, 0);
        if (n <= 0)
            return false;
        p += n;
        size -= n;
    }
    return !size;
}
static void transfer(int fd)
{
    uint8_t session_key[16];
    h5_critical_enter(&lock, 4000 + __LINE__);
    bool active = atomic_load(&enabled);
    uint32_t generation = update_generation;
    memcpy(session_key, key, sizeof(key));
    h5_critical_exit(&lock);
    if (!active) {
        send(fd, "DISABLED\n", 9, 0);
        return;
    }
    uint8_t nonce[32], header[68], expected[32], auth[68];
    if (H5_OTA_REQUIRE_PAIRING) {
        char greeting[73] = "H5OTA1 ";
        esp_fill_random(nonce, sizeof(nonce));
        hex(greeting + 7, nonce, 32);
        strcat(greeting, "\n");
        send(fd, greeting, strlen(greeting), 0);
    } else
        send(fd, "H5OTA0\n", 7, 0);
    if (!receive(fd, header, H5_OTA_REQUIRE_PAIRING ? sizeof(header) : 36))
        return;
    if (H5_OTA_REQUIRE_PAIRING) {
        memcpy(auth, nonce, 32);
        memcpy(auth + 32, header, 36);
        mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), session_key, sizeof(session_key), auth,
                        sizeof(auth), expected);
        if (!equal(expected, header + 36, 32)) {
            send(fd, "AUTH\n", 5, 0);
            return;
        }
    }
    uint32_t size =
        ((uint32_t)header[0] << 24) | ((uint32_t)header[1] << 16) | ((uint32_t)header[2] << 8) | header[3];
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (!partition || size < 1024 || size > partition->size) {
        send(fd, "SIZE\n", 5, 0);
        return;
    }
    // Closing or expiring update mode must not release the motion owner in
    // the gap between authentication and flash erase. Also reject a handshake
    // from a previous update session, even if the panel was reopened.
    h5_critical_enter(&lock, 4000 + __LINE__);
    bool accepted = atomic_load(&enabled) && generation == update_generation;
    if (accepted)
        atomic_store(&uploading, true);
    h5_critical_exit(&lock);
    if (!accepted) {
        send(fd, "DISABLED\n", 9, 0);
        return;
    }
    message(H5_OTA_REQUIRE_PAIRING ? "Receiving authenticated firmware" : "Receiving firmware (LAN pairing disabled)");
    esp_ota_handle_t ota = 0;
    esp_err_t result = esp_ota_begin(partition, size, &ota);
    if (result != ESP_OK) {
        send(fd, "BEGIN\n", 6, 0);
        atomic_store(&uploading, false);
        return;
    }
    send(fd, "READY\n", 6, 0);
    mbedtls_sha256_context digest;
    mbedtls_sha256_init(&digest);
    mbedtls_sha256_starts(&digest, 0);
    uint8_t buffer[4096], actual[32];
    uint32_t received = 0;
    while (received < size) {
        size_t n = size - received < sizeof(buffer) ? size - received : sizeof(buffer);
        if (!receive(fd, buffer, n)) {
            result = ESP_FAIL;
            break;
        }
        if (!received) {
            const esp_app_desc_t *description = (const esp_app_desc_t *)(buffer + sizeof(esp_image_header_t) +
                                                                         sizeof(esp_image_segment_header_t));
            if (buffer[0] != ESP_IMAGE_HEADER_MAGIC ||
                memcmp(description->project_name, "h5_grblhal_p4", 13)) {
                result = ESP_ERR_INVALID_ARG;
                break;
            }
        }
        mbedtls_sha256_update(&digest, buffer, n);
        if ((result = esp_ota_write(ota, buffer, n)) != ESP_OK)
            break;
        received += n;
        h5_critical_enter(&lock, 4000 + __LINE__);
        published.percent = received * 100 / size;
        h5_critical_exit(&lock);
    }
    mbedtls_sha256_finish(&digest, actual);
    mbedtls_sha256_free(&digest);
    if (result == ESP_OK && !equal(actual, header + 4, 32))
        result = ESP_ERR_INVALID_CRC;
    if (result == ESP_OK) {
        esp_log_level_set("esp_image", ESP_LOG_ERROR);
        result = esp_ota_end(ota);
        ota = 0;
        esp_log_level_set("esp_image", ESP_LOG_NONE);
    }
    if (result == ESP_OK)
        result = esp_ota_set_boot_partition(partition);
    if (ota)
        esp_ota_abort(ota);
    if (result == ESP_OK) {
        send(fd, "OK\n", 3, 0);
        message("Firmware verified; restarting");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
    char error[128];
    snprintf(error, sizeof(error), "INVALID:%s\n", esp_err_to_name(result));
    send(fd, error, strlen(error), 0);
    snprintf(error, sizeof(error), "Update failed (%s); previous firmware retained", esp_err_to_name(result));
    message(error);
    atomic_store(&uploading, false);
}
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
        esp_wifi_connect();
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        h5_critical_enter(&lock, 4000 + __LINE__);
        published.connected = false;
        h5_critical_exit(&lock);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = data;
        char ip[sizeof(published.ip)] = {0};
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&event->ip_info.ip));
        h5_critical_enter(&lock, 4000 + __LINE__);
        published.connected = true;
        memcpy(published.ip, ip, sizeof(ip));
        h5_critical_exit(&lock);
        message("Wi-Fi connected; updates available when idle");
    }
}
static void network_task(void *arg)
{
    if (esp_netif_init() != ESP_OK || esp_event_loop_create_default() != ESP_OK) {
        message("Network initialization failed");
        atomic_store(&initialized, true);
        vTaskDelete(NULL);
        return;
    }
    esp_netif_create_default_wifi_sta();
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL);
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&init) != ESP_OK) {
        message("Hosted Wi-Fi unavailable");
        atomic_store(&initialized, true);
        vTaskDelete(NULL);
        return;
    }
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    bool started = false;
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    struct sockaddr_in address = {
        .sin_family = AF_INET, .sin_port = htons(3232), .sin_addr.s_addr = htonl(INADDR_ANY)};
    struct timeval timeout = {.tv_sec = 1};
    setsockopt(listener, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) || listen(listener, 1)) {
        message("OTA listener unavailable");
        close(listener);
        atomic_store(&initialized, true);
        vTaskDelete(NULL);
        return;
    }
    fcntl(listener, F_SETFL, O_NONBLOCK);
    int observer = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    address.sin_port = htons(8080);
    if (observer >= 0 && (bind(observer, (struct sockaddr *)&address, sizeof(address)) || listen(observer, 2))) {
        close(observer); observer = -1;
    }
    if (observer >= 0) fcntl(observer, F_SETFL, O_NONBLOCK);
    uint32_t last_retry = 0;
    for (;;) {
        if (atomic_exchange(&reconfigure, false)) {
            wifi_config_t config = {0};
            h5_critical_enter(&lock, 4000 + __LINE__);
            memcpy(config.sta.ssid, wifi_ssid, sizeof(config.sta.ssid));
            memcpy(config.sta.password, wifi_password, sizeof(config.sta.password));
            h5_critical_exit(&lock);
            if (started)
                esp_wifi_disconnect();
            esp_wifi_set_config(WIFI_IF_STA, &config);
            if (!started) {
                esp_wifi_start();
                started = true;
            } else
                esp_wifi_connect();
        }
        atomic_store(&initialized, true);
        if (started && (uint32_t)(esp_timer_get_time() / 1000) - last_retry > 10000) {
            last_retry = esp_timer_get_time() / 1000;
            h5_update_status_t s;
            h5_update_snapshot(&s);
            if (!s.connected)
                esp_wifi_connect();
        }
        int read_fd = observer >= 0 ? accept(observer, NULL, NULL) : -1;
        if (read_fd >= 0) {
            struct timeval read_timeout = {.tv_sec = 1};
            setsockopt(read_fd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof(read_timeout));
            setsockopt(read_fd, SOL_SOCKET, SO_SNDTIMEO, &read_timeout, sizeof(read_timeout));
            h5_diagnostics_serve(read_fd);
            shutdown(read_fd, SHUT_RDWR);
            close(read_fd);
        }
        int fd = accept(listener, NULL, NULL);
        if (fd < 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        timeout.tv_sec = 5;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        transfer(fd);
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }
}
void h5_network_start(void)
{
    esp_ota_img_states_t state;
    validation_pending = esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
                         state == ESP_OTA_IMG_PENDING_VERIFY;
    if (validation_pending)
        h5_operation_claim(H5_OWNER_UPDATE);
    if (validation_pending)
        reject_validation = h5_storage_consume_ota_rejection();
    if (h5_storage_wifi(wifi_ssid, wifi_password))
        atomic_store(&reconfigure, true);
    message("Configure Wi-Fi through USB");
    xTaskCreatePinnedToCore(network_task, "H5_network", 12288, NULL, 2, NULL, 0);
}
void h5_network_poll(void)
{
    bool idle = state_get() == STATE_IDLE && h5_motion_idle() && !st_is_stepping() &&
                !plan_get_current_block() && !h5_cycle_busy() && h5_bridge_empty() && !h5_serial_pending();
    h5_diagnostics_poll(idle && !h5_update_active());
    uint32_t now = hal.get_elapsed_ticks();
    if (validation_pending && ((reject_validation && now > 5000) || now > 30000)) {
        message("Boot validation failed; rolling back");
        esp_ota_mark_app_invalid_rollback_and_reboot();
        return;
    }
    if (validation_pending && !reject_validation && idle && h5_ui_ready() && h5_storage_ready() &&
        !sys.alarm) {
        if (!validation_at)
            validation_at = now;
        if (now - validation_at > 5000 && esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
            validation_pending = false;
            h5_operation_release(H5_OWNER_UPDATE);
            message("New firmware passed boot checks");
        }
    }
    if (atomic_load(&requested) && idle && !atomic_load(&enabled) && !atomic_load(&uploading)) {
        uint8_t next_key[16] = {0};
        char next_text[33] = {0};
        if (H5_OTA_REQUIRE_PAIRING) {
            esp_fill_random(next_key, sizeof(next_key));
            hex(next_text, next_key, sizeof(next_key));
        }
        h5_critical_enter(&lock, 4000 + __LINE__);
        bool activate = atomic_load(&requested) && !atomic_load(&enabled) &&
                        !atomic_load(&uploading) && h5_operation_claim(H5_OWNER_UPDATE);
        if (activate) {
            memcpy(key, next_key, sizeof(key));
            memcpy(published.key, next_text, sizeof(next_text));
            published.percent = 0;
            update_generation++;
            atomic_store(&enabled, true);
            atomic_store(&requested, false);
            enabled_at = now;
        }
        h5_critical_exit(&lock);
        if (activate)
            message("Update mode; motion locked. Upload within five minutes");
    }
    h5_critical_enter(&lock, 4000 + __LINE__);
    bool expired = atomic_load(&enabled) && !atomic_load(&uploading) && now - enabled_at > 300000;
    if (expired) {
        atomic_store(&enabled, false);
        h5_operation_release(H5_OWNER_UPDATE);
    }
    h5_critical_exit(&lock);
    if (expired)
        message("Update mode expired");
}
status_code_t h5_network_command(sys_state_t state, char *line)
{
    if (!strcmp(line, "P4DIAG=1")) { h5_diagnostics_start(); return Status_OK; }
    if (!strcmp(line, "P4DIAG=0")) { h5_diagnostics_stop(); return Status_OK; }
    if (!strcmp(line, "P4OTATEST=REJECT"))
        return h5_storage_reject_next_ota(true) ? Status_OK : Status_IdleError;
    if (!strcmp(line, "P4OTATEST=CLEAR"))
        return h5_storage_reject_next_ota(false) ? Status_OK : Status_IdleError;
    if (!strcmp(line, "P4OTA=1")) {
        return h5_update_request() ? Status_OK : Status_IdleError;
    }
    if (!strcmp(line, "P4OTA=0")) {
        h5_update_cancel();
        return Status_OK;
    }
    if (!strcmp(line, "P4OTA")) {
        h5_update_status_t s;
        h5_update_snapshot(&s);
        char text[250];
        snprintf(
            text, sizeof(text),
            "[P4OTA:ACTIVE:%u|WIFI:%u|IP:%s|PAIRING:%u|KEY:%s|PARTITION:%s|PENDING_VERIFY:%u|PERCENT:%u|STAGE:%s]\r\n",
            s.active, s.connected, s.ip, s.pairing_required, s.active ? s.key : "", esp_ota_get_running_partition()->label,
            validation_pending, s.percent, s.message);
        hal.stream.write(text);
        return Status_OK;
    }
    if (!strncmp(line, "P4WIFI=", 7)) {
        if (state != STATE_IDLE || h5_cycle_busy() || atomic_load(&uploading))
            return Status_IdleError;
        char *password = strchr(line + 7, ',');
        if (!password)
            return Status_InvalidStatement;
        *password++ = 0;
        size_t a = strlen(line + 7) / 2, b = strlen(password) / 2;
        char ssid[33] = {0}, secret[65] = {0};
        if (!a || a > 32 || b > 63 || !unhex((uint8_t *)ssid, line + 7, a) ||
            !unhex((uint8_t *)secret, password, b) || strlen(ssid) != a || strlen(secret) != b)
            return Status_InvalidStatement;
        if (!h5_storage_set_wifi(ssid, secret))
            return Status_SettingReadFail;
        h5_critical_enter(&lock, 4000 + __LINE__);
        memcpy(wifi_ssid, ssid, sizeof(ssid));
        memcpy(wifi_password, secret, sizeof(secret));
        h5_critical_exit(&lock);
        atomic_store(&reconfigure, true);
        return Status_OK;
    }
    return Status_Unhandled;
}
