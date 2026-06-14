/*
 * IParam.h — type-erased base so the manager can hold parameters of any type.
 *
 * Independent reimplementation for waveshare_withrottle_dial. No source from the
 * upstream withrottle_dial project is reused here.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John (honzup)
 */
#ifndef WD_IPARAM_H
#define WD_IPARAM_H

#include <string>
#include "esp_err.h"

namespace params {

// Non-templated face of a parameter. ParamMgr keeps a list of these and drives
// them by name (list, persist, reload, reset, set-from-text) without knowing the
// concrete value type behind each one.
class IParam {
public:
    virtual ~IParam() = default;

    const std::string &getParamName() const { return m_name; }

    virtual bool        storedInFlash() const                  = 0;
    virtual std::string toString() const                       = 0;
    virtual esp_err_t   setFromString(const std::string &text) = 0;
    virtual esp_err_t   save()                                 = 0;
    virtual esp_err_t   getFromNvs()                           = 0;
    virtual esp_err_t   setToDefault()                         = 0;

protected:
    explicit IParam(const char *name);

    std::string m_name;
};

}  // namespace params

#endif  // WD_IPARAM_H
