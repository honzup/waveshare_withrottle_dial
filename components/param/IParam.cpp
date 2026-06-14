/*
 * IParam.cpp — base-class construction self-registers the parameter.
 *
 * Independent reimplementation for waveshare_withrottle_dial. No source from the
 * upstream withrottle_dial project is reused here.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John (honzup)
 */
#include "IParam.h"
#include "ParamMgr.h"

namespace params {

// Each parameter announces itself to the manager as it is constructed, so the
// "param" console command can enumerate and edit it without a central list.
IParam::IParam(const char *name) : m_name(name)
{
    ParamMgr::getInstance().registerParam(this);
}

}  // namespace params
