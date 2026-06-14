/*
 * NvsMgr.cpp — implementation of the NVS blob gateway.
 *
 * Independent reimplementation for waveshare_withrottle_dial, written against
 * the public ESP-IDF nvs_flash / nvs interface. No source from the upstream
 * withrottle_dial project is reused here.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John (honzup)
 */
#include "NvsMgr.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace params {

namespace {
constexpr const char *kNamespace = "storage";
constexpr const char *kTag       = "NvsMgr";

// RAII guard so an open NVS handle is always closed, on every return path.
class Handle {
public:
    Handle(nvs_open_mode_t mode, esp_err_t &err)
    {
        err = nvs_open(kNamespace, mode, &m_handle);
        m_open = (err == ESP_OK);
        if (!m_open) {
            ESP_LOGE(kTag, "nvs_open failed: %s", esp_err_to_name(err));
        }
    }
    ~Handle()
    {
        if (m_open) {
            nvs_close(m_handle);
        }
    }
    operator nvs_handle_t() const { return m_handle; }
    bool ok() const { return m_open; }

private:
    nvs_handle_t m_handle{};
    bool         m_open{false};
};

// Take/release the instance mutex with a scope guard.
class Lock {
public:
    explicit Lock(SemaphoreHandle_t s) : m_s(s) { xSemaphoreTake(m_s, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(m_s); }

private:
    SemaphoreHandle_t m_s;
};
}  // namespace

NvsMgr::NvsMgr()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // Partition layout changed or is full — start clean and retry once.
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    m_lock = xSemaphoreCreateMutex();
    configASSERT(m_lock != nullptr);
}

NvsMgr::~NvsMgr()
{
    if (m_lock != nullptr) {
        vSemaphoreDelete(m_lock);
    }
}

esp_err_t NvsMgr::set(const char *name, const void *value, size_t size)
{
    Lock guard(m_lock);

    esp_err_t err;
    Handle    h(NVS_READWRITE, err);
    if (!h.ok()) {
        return err;
    }

    err = nvs_set_blob(h, name, value, size);
    if (err == ESP_OK) {
        err = nvs_commit(h);
        ESP_LOGD(kTag, "set '%s' (%u bytes)", name, (unsigned) size);
    } else {
        ESP_LOGE(kTag, "set '%s' failed: %s", name, esp_err_to_name(err));
    }
    return err;
}

esp_err_t NvsMgr::get(const char *name, void *value, size_t &size)
{
    Lock guard(m_lock);

    esp_err_t err;
    Handle    h(NVS_READONLY, err);
    if (!h.ok()) {
        size = 0;
        return err;
    }

    err = nvs_get_blob(h, name, value, &size);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGE(kTag, "get '%s' failed: %s", name, esp_err_to_name(err));
        }
        size = 0;
    } else {
        ESP_LOGD(kTag, "get '%s' (%u bytes)", name, (unsigned) size);
    }
    return err;
}

esp_err_t NvsMgr::querySize(const char *name, size_t &size)
{
    Lock guard(m_lock);

    esp_err_t err;
    Handle    h(NVS_READONLY, err);
    if (!h.ok()) {
        size = 0;
        return err;
    }

    // Passing a null buffer asks NVS for the stored length only.
    size = 0;
    err  = nvs_get_blob(h, name, nullptr, &size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(kTag, "querySize '%s' failed: %s", name, esp_err_to_name(err));
    }
    return err;
}

esp_err_t NvsMgr::erase(const char *name)
{
    Lock guard(m_lock);

    esp_err_t err;
    Handle    h(NVS_READWRITE, err);
    if (!h.ok()) {
        return err;
    }

    err = nvs_erase_key(h, name);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(kTag, "erase '%s' failed: %s", name, esp_err_to_name(err));
    }
    return err;
}

esp_err_t NvsMgr::eraseAll()
{
    Lock guard(m_lock);

    esp_err_t err;
    Handle    h(NVS_READWRITE, err);
    if (!h.ok()) {
        return err;
    }

    err = nvs_erase_all(h);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    } else {
        ESP_LOGE(kTag, "eraseAll failed: %s", esp_err_to_name(err));
    }
    return err;
}

}  // namespace params
