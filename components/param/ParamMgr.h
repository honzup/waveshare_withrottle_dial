/*
 * ParamMgr.h — registry of all parameters plus the "param" console command.
 *
 * Independent reimplementation for waveshare_withrottle_dial. No source from the
 * upstream withrottle_dial project is reused here.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John (honzup)
 */
#ifndef WD_PARAM_MGR_H
#define WD_PARAM_MGR_H

#include <string>
#include <vector>

#include "IParam.h"
#include "esp_err.h"

namespace params {

// Owns nothing — just a flat index of every Param that has been constructed, so
// they can be listed and edited by name from the serial console.
class ParamMgr {
public:
    static ParamMgr &getInstance()
    {
        static ParamMgr s;
        return s;
    }

    void      listAll() const;            // print name = value for each param
    esp_err_t saveAll();                  // flush every param to NVS
    esp_err_t readAll();                  // reload every param from NVS
    esp_err_t eraseAll();                 // wipe the whole NVS namespace
    esp_err_t resetDefaultAll();          // restore every param to its default

    esp_err_t setParam(const std::string &name, const std::string &value);
    IParam   *getParam(const std::string &name) const;

    ParamMgr(const ParamMgr &)            = delete;
    ParamMgr &operator=(const ParamMgr &) = delete;

private:
    ParamMgr();

    void registerParam(IParam *param);
    friend class IParam;

    std::vector<IParam *> m_params;
};

}  // namespace params

#endif  // WD_PARAM_MGR_H
