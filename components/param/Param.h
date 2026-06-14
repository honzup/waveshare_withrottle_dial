/*
 * Param.h — a single, optionally NVS-backed, typed configuration value.
 *
 * Independent reimplementation for waveshare_withrottle_dial. The class shape
 * (params::Param<T>, params::password) mirrors what this project's own code
 * calls, but every implementation body here is newly authored against the
 * ESP-IDF NVS API. No source from the upstream withrottle_dial project is
 * reused.
 *
 * On-flash layout is deliberately byte-compatible with previously stored values:
 * POD values are saved as a fixed-size blob, text values as their raw bytes
 * (no trailing NUL).
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John (honzup)
 */
#ifndef WD_PARAM_H
#define WD_PARAM_H

#include <algorithm>
#include <cstdlib>
#include <string>
#include <type_traits>
#include <utility>

#include "IParam.h"
#include "NvsMgr.h"

namespace params {

// A string whose textual rendering is masked, for secrets like Wi-Fi keys.
struct password : public std::string {
    using std::string::string;
    password() = default;
    password(const std::string &s) : std::string(s) {}
};

template <typename T>
class Param : public IParam {
public:
    Param(const char *name, T defaultValue, bool storeInFlash = true)
        : IParam(name),
          m_default(defaultValue),
          m_persist(storeInFlash),
          m_value(std::move(defaultValue))
    {
        if (m_persist) {
            // Best effort: an absent key leaves the in-RAM default in place.
            getFromNvs();
        }
    }

    T       &get() { return m_value; }
    const T &get() const { return m_value; }

    esp_err_t set(T value)
    {
        m_value = std::move(value);
        return save();
    }

    bool storedInFlash() const override { return m_persist; }

    esp_err_t save() override
    {
        if (!m_persist) {
            return ESP_ERR_INVALID_STATE;
        }
        return writeValue();
    }

    esp_err_t getFromNvs() override
    {
        if (!m_persist) {
            return ESP_ERR_INVALID_STATE;
        }
        return readValue();
    }

    esp_err_t setToDefault() override { return set(m_default); }

    std::string toString() const override;
    esp_err_t   setFromString(const std::string &text) override;

private:
    // Type-specific persistence. The primary template handles trivially-copyable
    // values (int, bool, ...) as a fixed-size blob; text types are specialised
    // below.
    esp_err_t writeValue()
    {
        static_assert(std::is_trivially_copyable<T>::value,
                      "non-POD Param<T> needs an explicit specialisation");
        return NvsMgr::getInstance().set(m_name.c_str(), &m_value, sizeof(m_value));
    }
    esp_err_t readValue()
    {
        size_t len = sizeof(m_value);
        return NvsMgr::getInstance().get(m_name.c_str(), &m_value, len);
    }

    const T    m_default;
    const bool m_persist;
    T          m_value;
};

// ---- text persistence shared by std::string and password -------------------

namespace detail {
inline esp_err_t writeText(const std::string &name, const std::string &value)
{
    return NvsMgr::getInstance().set(name.c_str(), value.data(), value.size());
}

inline esp_err_t readText(const std::string &name, std::string &out)
{
    size_t    len = 0;
    esp_err_t err = NvsMgr::getInstance().querySize(name.c_str(), len);
    if (err != ESP_OK) {
        return err;  // key missing or read error — keep current value
    }
    std::string buf;
    buf.resize(len);
    if (len > 0) {
        err = NvsMgr::getInstance().get(name.c_str(), &buf[0], len);
        if (err != ESP_OK) {
            return err;
        }
        buf.resize(len);
    }
    out = std::move(buf);
    return ESP_OK;
}
}  // namespace detail

// ---- std::string -----------------------------------------------------------

template <>
inline esp_err_t Param<std::string>::writeValue()
{
    return detail::writeText(m_name, m_value);
}
template <>
inline esp_err_t Param<std::string>::readValue()
{
    return detail::readText(m_name, m_value);
}
template <>
inline std::string Param<std::string>::toString() const
{
    return m_value;
}
template <>
inline esp_err_t Param<std::string>::setFromString(const std::string &text)
{
    return set(text);
}

// ---- password (masked) -----------------------------------------------------

template <>
inline esp_err_t Param<password>::writeValue()
{
    return detail::writeText(m_name, m_value);
}
template <>
inline esp_err_t Param<password>::readValue()
{
    std::string tmp;
    esp_err_t   err = detail::readText(m_name, tmp);
    if (err == ESP_OK) {
        m_value = password(tmp);
    }
    return err;
}
template <>
inline std::string Param<password>::toString() const
{
    return std::string(std::min<size_t>(m_value.size(), 50), '*');
}
template <>
inline esp_err_t Param<password>::setFromString(const std::string &text)
{
    return set(password(text));
}

// ---- int -------------------------------------------------------------------

template <>
inline std::string Param<int>::toString() const
{
    return std::to_string(m_value);
}
template <>
inline esp_err_t Param<int>::setFromString(const std::string &text)
{
    char     *end = nullptr;
    long      v   = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    return set(static_cast<int>(v));
}

// ---- bool ------------------------------------------------------------------

template <>
inline std::string Param<bool>::toString() const
{
    return m_value ? "true" : "false";
}
template <>
inline esp_err_t Param<bool>::setFromString(const std::string &text)
{
    if (text == "true" || text == "1") {
        return set(true);
    }
    if (text == "false" || text == "0") {
        return set(false);
    }
    return ESP_ERR_INVALID_ARG;
}

// ---- generic fallback for any other T --------------------------------------

template <typename T>
inline std::string Param<T>::toString() const
{
    return std::string{};
}
template <typename T>
inline esp_err_t Param<T>::setFromString(const std::string &)
{
    return ESP_ERR_NOT_SUPPORTED;
}

}  // namespace params

#endif  // WD_PARAM_H
