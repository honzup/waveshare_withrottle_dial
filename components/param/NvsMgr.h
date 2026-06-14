/*
 * NvsMgr.h — thin, thread-safe gateway over the ESP-IDF NVS blob API.
 *
 * Independent reimplementation for waveshare_withrottle_dial, written against
 * the public ESP-IDF nvs_flash / nvs interface. No source from the upstream
 * withrottle_dial project is reused here.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John (honzup)
 */
#ifndef WD_NVS_MGR_H
#define WD_NVS_MGR_H

#include <cstddef>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace params {

// Single point of access to one NVS namespace. Values are stored as raw blobs so
// this layer stays type-agnostic; the typed Param<> layer above decides how a
// value turns into bytes. Every operation is serialised behind a mutex since NVS
// handles must not be shared concurrently across tasks.
class NvsMgr {
public:
    static NvsMgr &getInstance()
    {
        static NvsMgr s;
        return s;
    }

    // Store `size` bytes of `value` under `name`, overwriting any prior value.
    esp_err_t set(const char *name, const void *value, size_t size);

    // Read a value into `value`. On entry `size` holds the buffer capacity; on
    // success it is rewritten with the number of bytes read. On any failure
    // `size` is reset to 0.
    esp_err_t get(const char *name, void *value, size_t &size);

    // Query the stored byte length of `name` without copying it out.
    esp_err_t querySize(const char *name, size_t &size);

    esp_err_t erase(const char *name);  // remove one key
    esp_err_t eraseAll();               // clear the whole namespace

    NvsMgr(const NvsMgr &)            = delete;
    NvsMgr &operator=(const NvsMgr &) = delete;

private:
    NvsMgr();
    ~NvsMgr();

    SemaphoreHandle_t m_lock;
};

}  // namespace params

#endif  // WD_NVS_MGR_H
