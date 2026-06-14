#include "udp_log.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

static int s_sock = -1;
static struct sockaddr_in s_dest;

static int udp_vprintf(const char* fmt, va_list args)
{
    // Use va_copy so args is still valid for the vprintf call below.
    va_list args_udp;
    va_copy(args_udp, args);
    char buf[384];
    int n = vsnprintf(buf, sizeof(buf), fmt, args_udp);
    va_end(args_udp);
    if (s_sock >= 0 && n > 0) {
        int send_len = n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1;
        sendto(s_sock, buf, send_len, 0, (struct sockaddr*)&s_dest, sizeof(s_dest));
    }
    return vprintf(fmt, args);
}

void udp_log_start(const char* host_ip, unsigned short port)
{
    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) return;
    int bc = 1;
    setsockopt(s_sock, SOL_SOCKET, SO_BROADCAST, &bc, sizeof(bc));
    memset(&s_dest, 0, sizeof(s_dest));
    s_dest.sin_family      = AF_INET;
    s_dest.sin_port        = htons(port);
    s_dest.sin_addr.s_addr = inet_addr(host_ip);
    esp_log_set_vprintf(udp_vprintf);
}
