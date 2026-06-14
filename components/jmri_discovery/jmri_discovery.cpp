#include "jmri_discovery.h"
#include "mdns.h"
#include "esp_netif_ip_addr.h"
#include "esp_log.h"

namespace jmri_discovery {

static const char* TAG = "jmri_disc";
static bool s_inited = false;

std::vector<JmriServer> discover(uint32_t timeout_ms) {
    std::vector<JmriServer> out;
    // Called only from the WiThrottle client task; s_inited needs no lock.
    if (!s_inited) {
        if (mdns_init() != ESP_OK) { ESP_LOGW(TAG, "mdns_init failed"); return out; }
        s_inited = true;
    }
    mdns_result_t* results = nullptr;
    esp_err_t q = mdns_query_ptr("_withrottle", "_tcp", timeout_ms, 8, &results);
    if (q != ESP_OK) {
        ESP_LOGW(TAG, "mdns_query_ptr failed: %s", esp_err_to_name(q));
        return out;
    }
    if (!results) return out; // ESP_OK but zero responses
    for (mdns_result_t* r = results; r; r = r->next) {
        JmriServer s;
        s.name = r->instance_name ? r->instance_name : "";
        s.port = r->port;
        if (r->addr && r->addr->addr.type == ESP_IPADDR_TYPE_V4) {
            char buf[16] = {0};
            esp_ip4addr_ntoa(&r->addr->addr.u_addr.ip4, buf, sizeof(buf));
            s.ip = buf;
        }
        if (!s.ip.empty()) out.push_back(s);
    }
    mdns_query_results_free(results);
    ESP_LOGI(TAG, "discovered %d JMRI server(s)", (int)out.size());
    return out;
}

} // namespace jmri_discovery
