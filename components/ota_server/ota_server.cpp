#include "ota_server.h"

#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>

static const char* TAG = "espota";

#define ESPOTA_PORT   3232
#define ESPOTA_CHUNK  1024  // matches espota.py chunk size

static bool do_flash(int tcp_sock, int fw_size)
{
    const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
    if (!part)
    {
        ESP_LOGE(TAG, "No OTA partition available");
        return false;
    }

    esp_ota_handle_t handle = 0;
    if (esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &handle) != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_begin failed");
        return false;
    }

    static uint8_t buf[ESPOTA_CHUNK];
    int total = 0;

    while (total < fw_size)
    {
        // Read exactly one espota chunk (1024 bytes, or remainder) so we stay
        // in sync with espota.py's send-chunk / wait-OK ping-pong protocol.
        int want = (fw_size - total < ESPOTA_CHUNK) ? (fw_size - total) : ESPOTA_CHUNK;
        int got  = 0;
        while (got < want)
        {
            int n = recv(tcp_sock, buf + got, want - got, 0);
            if (n <= 0)
            {
                ESP_LOGE(TAG, "recv failed at offset %d (errno %d)", total + got, errno);
                esp_ota_abort(handle);
                return false;
            }
            got += n;
        }

        if (esp_ota_write(handle, buf, got) != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_ota_write failed at offset %d", total);
            esp_ota_abort(handle);
            return false;
        }
        total += got;
        send(tcp_sock, "OK", 2, 0);

        if ((total % (64 * 1024)) < ESPOTA_CHUNK)
            ESP_LOGI(TAG, "OTA: %d / %d bytes", total, fw_size);
    }

    esp_err_t err = esp_ota_end(handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(err));
        return false;
    }
    if (esp_ota_set_boot_partition(part) != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed");
        return false;
    }
    return true;
}

static void espota_task(void*)
{
    int udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp < 0)
    {
        ESP_LOGE(TAG, "UDP socket failed");
        vTaskDelete(nullptr);
        return;
    }

    int opt = 1;
    setsockopt(udp, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in local = {};
    local.sin_family         = AF_INET;
    local.sin_port           = htons(ESPOTA_PORT);
    local.sin_addr.s_addr    = htonl(INADDR_ANY);

    if (bind(udp, (struct sockaddr*)&local, sizeof(local)) < 0)
    {
        ESP_LOGE(TAG, "UDP bind failed (errno %d)", errno);
        close(udp);
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "espota listening on UDP %d", ESPOTA_PORT);

    for (;;)
    {
        char             inv[128];
        struct sockaddr_in sender = {};
        socklen_t         slen   = sizeof(sender);

        int n = recvfrom(udp, inv, sizeof(inv) - 1, 0,
                         (struct sockaddr*)&sender, &slen);
        if (n <= 0) continue;
        inv[n] = '\0';

        // espota invitation: "<cmd> <host_port> <fw_size> <md5>\n"
        int  cmd = -1, host_port = 0, fw_size = 0;
        char md5[33] = {};
        if (sscanf(inv, "%d %d %d %32s", &cmd, &host_port, &fw_size, md5) < 3)
        {
            ESP_LOGW(TAG, "Ignoring unknown UDP message");
            continue;
        }
        if (cmd != 0)
        {
            ESP_LOGW(TAG, "Unsupported OTA command %d (only FLASH=0 supported)", cmd);
            sendto(udp, "FAIL", 4, 0, (struct sockaddr*)&sender, slen);
            continue;
        }

        char host_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sender.sin_addr, host_ip, sizeof(host_ip));
        ESP_LOGI(TAG, "OTA invite from %s: host_port=%d size=%d", host_ip, host_port, fw_size);

        sendto(udp, "OK", 2, 0, (struct sockaddr*)&sender, slen);

        // espota.py starts a TCP server on host_port and waits for us to connect.
        struct sockaddr_in host = {};
        host.sin_family         = AF_INET;
        host.sin_port           = htons(host_port);
        host.sin_addr.s_addr    = sender.sin_addr.s_addr;

        int tcp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (tcp < 0) { ESP_LOGE(TAG, "TCP socket failed"); continue; }

        struct timeval tv = {10, 0};
        setsockopt(tcp, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        if (connect(tcp, (struct sockaddr*)&host, sizeof(host)) < 0)
        {
            ESP_LOGE(TAG, "TCP connect to %s:%d failed (errno %d)", host_ip, host_port, errno);
            close(tcp);
            continue;
        }

        ESP_LOGI(TAG, "Flashing %d bytes from %s:%d", fw_size, host_ip, host_port);

        if (do_flash(tcp, fw_size))
        {
            close(tcp);
            ESP_LOGI(TAG, "OTA complete – restarting");
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        }
        else
        {
            close(tcp);
            ESP_LOGE(TAG, "OTA failed");
        }
    }
}

void ota_server_start(void)
{
    xTaskCreate(espota_task, "espota", 8 * 1024, nullptr, 5, nullptr);
}
