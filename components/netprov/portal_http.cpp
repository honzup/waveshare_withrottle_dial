#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ParamMgr.h"
#include "netprov_validation.h"
#include <string>
#include <vector>

static const char* TAG = "portal_http";
static httpd_handle_t s_server = nullptr;

static const char FORM[] =
"<!DOCTYPE html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>WiThrottle Setup</title><style>body{font-family:sans-serif;margin:1.5em;max-width:30em}"
"label{display:block;margin:.6em 0 .2em}input,select{width:100%;padding:.5em;box-sizing:border-box}"
"button{margin-top:1em;padding:.7em;width:100%;font-size:1.1em}</style></head><body>"
"<h2>WiThrottle Setup</h2><form method=POST action=/save>"
"<label>Wi-Fi network</label><select name=ssid id=ssid></select>"
"<label>Password</label><input name=password type=password>"
"<label>JMRI IP (optional - auto-discovered if blank)</label><input name=jmri_ip placeholder=192.168.1.46>"
"<label>JMRI port</label><input name=jmri_port value=12090>"
"<button type=submit>Save &amp; restart</button></form>"
"<script>fetch('/scan').then(r=>r.json()).then(a=>{let s=document.getElementById('ssid');"
"a.forEach(n=>{let o=document.createElement('option');o.text=n;o.value=n;s.add(o)})});</script>"
"</body></html>";

static std::string url_decode(const std::string& in) {
    std::string out; out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '+') out += ' ';
        else if (in[i] == '%' && i + 2 < in.size()) {
            auto hex = [](char c)->int{ if(c>='0'&&c<='9')return c-'0';
                if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return 0; };
            out += (char)(hex(in[i+1]) * 16 + hex(in[i+2])); i += 2;
        } else out += in[i];
    }
    return out;
}
static std::string field(const std::string& body, const std::string& key) {
    std::string k = key + "=";
    size_t p = body.find(k);
    while (p != std::string::npos && p != 0 && body[p-1] != '&') p = body.find(k, p + 1);
    if (p == std::string::npos) return "";
    p += k.size();
    size_t e = body.find('&', p);
    return url_decode(body.substr(p, e == std::string::npos ? std::string::npos : e - p));
}

static esp_err_t get_root(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, FORM, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t get_scan(httpd_req_t* req) {
    uint16_t n = 0;
    // Blocking all-channel scan: briefly hops channels, which can blip the
    // connected provisioning client. Acceptable for a one-off setup step.
    esp_err_t err = esp_wifi_scan_start(nullptr, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "wifi scan failed: %s", esp_err_to_name(err));
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, "[]", HTTPD_RESP_USE_STRLEN);
    }
    esp_wifi_scan_get_ap_num(&n);
    if (n > 20) n = 20;
    std::vector<wifi_ap_record_t> recs(n);
    esp_wifi_scan_get_ap_records(&n, recs.data());
    std::string json = "[";
    for (uint16_t i = 0; i < n; ++i) {
        if (i) json += ",";
        json += '"';
        // Escape for JSON so an SSID containing " or \ can't break the page.
        for (const char* c = (const char*) recs[i].ssid; *c; ++c) {
            if (*c == '"' || *c == '\\') json += '\\';
            json += *c;
        }
        json += '"';
    }
    json += "]";
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.c_str(), HTTPD_RESP_USE_STRLEN);
}

static esp_err_t post_save(httpd_req_t* req) {
    std::string body;
    body.resize(req->content_len);
    int got = 0;
    while (got < (int)req->content_len) {
        int r = httpd_req_recv(req, &body[got], req->content_len - got);
        if (r <= 0) { httpd_resp_send_500(req); return ESP_FAIL; }
        got += r;
    }
    std::string ssid = field(body, "ssid");
    std::string pass = field(body, "password");
    std::string ip   = field(body, "jmri_ip");
    std::string port = field(body, "jmri_port");

    using namespace netprov;
    uint16_t p = 0;
    bool ok = valid_ssid(ssid) && valid_password(pass)
              && (ip.empty()   || valid_ipv4(ip))
              && (port.empty() || valid_port(port, p));
    if (!ok) {
        httpd_resp_set_type(req, "text/html");
        return httpd_resp_send(req,
            "<html><body><h3>Invalid input</h3><a href=/>Back</a></body></html>",
            HTTPD_RESP_USE_STRLEN);
    }

    auto& pm = params::ParamMgr::getInstance();
    pm.setParam("wifi_ssid", ssid);
    pm.setParam("wifi_password", pass);
    if (!ip.empty())   pm.setParam("jmri_ip", ip);
    if (!port.empty()) pm.setParam("jmri_port", port);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req,
        "<html><body><h3>Saved. Restarting...</h3></body></html>", HTTPD_RESP_USE_STRLEN);
    ESP_LOGI(TAG, "Provisioned ssid='%s'; restarting", ssid.c_str());
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
    return ESP_OK;
}

// Redirect any other path to the form (OS captive-portal probes).
static esp_err_t get_redirect(httpd_req_t* req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, "", 0);
}

void portal_http_start(void) {
    if (s_server) return;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port      = 80;
    cfg.uri_match_fn     = httpd_uri_match_wildcard;
    cfg.max_uri_handlers = 8;
    if (httpd_start(&s_server, &cfg) != ESP_OK) { ESP_LOGE(TAG, "httpd_start failed"); return; }
    httpd_uri_t u_root   = { .uri = "/",     .method = HTTP_GET,  .handler = get_root,     .user_ctx = nullptr };
    httpd_uri_t u_scan   = { .uri = "/scan", .method = HTTP_GET,  .handler = get_scan,     .user_ctx = nullptr };
    httpd_uri_t u_save   = { .uri = "/save", .method = HTTP_POST, .handler = post_save,    .user_ctx = nullptr };
    httpd_uri_t u_any    = { .uri = "/*",    .method = HTTP_GET,  .handler = get_redirect, .user_ctx = nullptr };
    httpd_register_uri_handler(s_server, &u_root);
    httpd_register_uri_handler(s_server, &u_scan);
    httpd_register_uri_handler(s_server, &u_save);
    httpd_register_uri_handler(s_server, &u_any);
}
void portal_http_stop(void) {
    if (s_server) { httpd_stop(s_server); s_server = nullptr; }
}
