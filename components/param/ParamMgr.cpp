/*
 * ParamMgr.cpp — parameter registry and the "param" serial-console command.
 *
 * Independent reimplementation for waveshare_withrottle_dial, written against
 * the ESP-IDF esp_console / argtable3 API. No source from the upstream
 * withrottle_dial project is reused here.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John (honzup)
 */
#include "ParamMgr.h"

#include <cstdio>
#include <cstring>

#include "NvsMgr.h"
#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "nvs.h"

namespace params {

namespace {
void register_param_command();  // forward decl; defined at end of file
}

ParamMgr::ParamMgr()
{
    register_param_command();
}

void ParamMgr::registerParam(IParam *param)
{
    if (param != nullptr) {
        m_params.push_back(param);
    }
}

void ParamMgr::listAll() const
{
    // Persistent params in green, volatile ones in yellow.
    static const char *kGreen  = "\033[32m";
    static const char *kYellow = "\033[33m";
    static const char *kReset  = "\033[0m";

    std::printf("---------------------------- params ----------------------------\n");
    for (IParam *p : m_params) {
        const char *colour = p->storedInFlash() ? kGreen : kYellow;
        std::printf("%s%s = %s%s\n", colour, p->getParamName().c_str(), p->toString().c_str(),
                    kReset);
    }
    std::printf("----------------------------------------------------------------\n");
}

esp_err_t ParamMgr::saveAll()
{
    for (IParam *p : m_params) {
        esp_err_t err = p->save();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;  // skip volatile params (INVALID_STATE), fail on real errors
        }
    }
    return ESP_OK;
}

esp_err_t ParamMgr::readAll()
{
    for (IParam *p : m_params) {
        esp_err_t err = p->getFromNvs();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE && err != ESP_ERR_NVS_NOT_FOUND) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t ParamMgr::eraseAll()
{
    return NvsMgr::getInstance().eraseAll();
}

esp_err_t ParamMgr::resetDefaultAll()
{
    for (IParam *p : m_params) {
        esp_err_t err = p->setToDefault();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }
    }
    return ESP_OK;
}

IParam *ParamMgr::getParam(const std::string &name) const
{
    for (IParam *p : m_params) {
        if (p->getParamName() == name) {
            return p;
        }
    }
    return nullptr;
}

esp_err_t ParamMgr::setParam(const std::string &name, const std::string &value)
{
    IParam *p = getParam(name);
    if (p == nullptr) {
        return ESP_ERR_NOT_FOUND;
    }
    return p->setFromString(value);
}

// --------------------------------------------------------------------------
// "param" console command
//   param list | read | save | erase
//   param default [<name>]
//   param set <name> <value>
// --------------------------------------------------------------------------
namespace {

struct {
    struct arg_str *action;
    struct arg_str *name;
    struct arg_str *value;
    struct arg_end *end;
} s_args;

int param_command(int argc, char **argv)
{
    if (arg_parse(argc, argv, reinterpret_cast<void **>(&s_args)) != 0) {
        arg_print_errors(stderr, s_args.end, argv[0]);
        return 1;
    }

    const std::string action = s_args.action->sval[0];
    const std::string name   = s_args.name->count ? s_args.name->sval[0] : "";
    const std::string value  = s_args.value->count ? s_args.value->sval[0] : "";
    ParamMgr         &mgr     = ParamMgr::getInstance();

    if (action == "list") {
        mgr.listAll();
    } else if (action == "read") {
        mgr.readAll();
    } else if (action == "save") {
        mgr.saveAll();
    } else if (action == "erase") {
        mgr.eraseAll();
    } else if (action == "default") {
        if (name.empty()) {
            mgr.resetDefaultAll();
        } else if (IParam *p = mgr.getParam(name)) {
            p->setToDefault();
        } else {
            std::printf("unknown param '%s'\n", name.c_str());
            return 1;
        }
    } else if (action == "set") {
        if (name.empty() || value.empty()) {
            std::printf("usage: param set <name> <value>\n");
            return 1;
        }
        if (mgr.setParam(name, value) != ESP_OK) {
            std::printf("could not set '%s'\n", name.c_str());
            return 1;
        }
    } else {
        std::printf("usage: param list|read|save|erase|default [name]|set <name> <value>\n");
        return 1;
    }
    return 0;
}

void register_param_command()
{
    s_args.action = arg_str1(nullptr, nullptr, "<action>",
                             "list | read | save | erase | default | set");
    s_args.name   = arg_str0(nullptr, nullptr, "<name>", "parameter name");
    s_args.value  = arg_str0(nullptr, nullptr, "<value>", "value (for 'set')");
    s_args.end    = arg_end(3);

    const esp_console_cmd_t cmd = {
        .command        = "param",
        .help           = "inspect or edit stored parameters",
        .hint           = nullptr,
        .func           = &param_command,
        .argtable       = &s_args,
        .func_w_context = nullptr,
        .context        = nullptr,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

}  // namespace

}  // namespace params
