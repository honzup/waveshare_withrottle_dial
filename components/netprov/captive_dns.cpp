#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include <string.h>

static const char* TAG = "captive_dns";
static TaskHandle_t s_task = nullptr;
static volatile bool s_run = false;

// NOTE: This is a deliberately minimal captive-portal responder. It assumes a
// single-question query (QNAME at offset 12) and appends one A record after the
// original packet. Queries carrying EDNS/OPT records or multiple questions will
// get a technically-malformed reply; that's acceptable here — the OS portal probe
// still resolves to 192.168.4.1 and pops the page.
// Answer every A query with 192.168.4.1 (the SoftAP gateway) so the phone's
// captive-portal check resolves to us and pops the config page.
static void dns_task(void*) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { ESP_LOGE(TAG, "socket failed"); s_task = nullptr; vTaskDelete(nullptr); return; }
    struct sockaddr_in local = {};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(53);
    if (bind(sock, (struct sockaddr*) &local, sizeof(local)) < 0) {
        ESP_LOGE(TAG, "bind 53 failed"); close(sock); s_task = nullptr; vTaskDelete(nullptr); return;
    }
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t buf[512];
    while (s_run) {
        struct sockaddr_in from = {};
        socklen_t flen = sizeof(from);
        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*) &from, &flen);
        if (n < (int) sizeof(uint16_t) * 6) continue; // too short for a header + question

        // Build a minimal response: copy the query, set QR + AA flags, ANCOUNT=1,
        // and append an A record pointing the queried name (compressed) to .4.1.
        buf[2] |= 0x80;            // QR = response
        buf[3]  = 0x00;            // RA cleared, RCODE 0
        buf[7]  = 0x01;            // ANCOUNT = 1 (QDCOUNT already 1 from the query)

        int len = n;
        uint8_t ans[] = {
            0xC0, 0x0C,            // name: pointer to the question (offset 12)
            0x00, 0x01,            // TYPE A
            0x00, 0x01,            // CLASS IN
            0x00, 0x00, 0x00, 0x3C,// TTL 60s
            0x00, 0x04,            // RDLENGTH 4
            192, 168, 4, 1         // RDATA 192.168.4.1
        };
        if (len + (int) sizeof(ans) <= (int) sizeof(buf)) {
            memcpy(buf + len, ans, sizeof(ans));
            len += sizeof(ans);
            sendto(sock, buf, len, 0, (struct sockaddr*) &from, flen);
        }
    }
    close(sock);
    s_task = nullptr;
    vTaskDelete(nullptr);
}

void captive_dns_start(void) {
    if (s_task) return;
    s_run = true;
    xTaskCreate(dns_task, "captive_dns", 4 * 1024, nullptr, 4, &s_task);
}
// Best-effort stop: clears the run flag; the task exits within one recv timeout
// (~1s) and then closes the socket. Not used in the normal flow (provisioning ends
// with esp_restart()); only safe to pair with a later start after the task has exited.
void captive_dns_stop(void) { s_run = false; }
