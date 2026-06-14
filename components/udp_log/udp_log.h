#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// Start UDP log forwarder. All esp_log output is sent as UDP datagrams to
// host_ip:port (use broadcast 255.255.255.255 if host IP is unknown).
void udp_log_start(const char* host_ip, unsigned short port);

#ifdef __cplusplus
}
#endif
