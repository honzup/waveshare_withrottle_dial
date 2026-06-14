/*
 * ConsoleCommands.cpp — serial REPL bring-up and built-in debug commands.
 *
 * Independent reimplementation for waveshare_withrottle_dial, written against
 * the ESP-IDF esp_console / FreeRTOS APIs. No source from the upstream
 * withrottle_dial project is reused here.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John (honzup)
 */
#include "ConsoleCommands.h"

#include <cstdio>

#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace {

#ifdef CONFIG_CONSOLE_COMMANDS_HEAP
size_t g_heap_at_boot = 0;

int cmd_heap(int, char **)
{
    const size_t free_now = esp_get_free_heap_size();
    const size_t low_water = esp_get_minimum_free_heap_size();
    const size_t biggest  = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    const float  used_pct =
        g_heap_at_boot ? 100.0f * (1.0f - static_cast<float>(free_now) / g_heap_at_boot) : 0.0f;

    std::printf("heap: free %u of %u B (%.1f%% used), low-water %u B, largest block %u B\n",
                static_cast<unsigned>(free_now), static_cast<unsigned>(g_heap_at_boot), used_pct,
                static_cast<unsigned>(low_water), static_cast<unsigned>(biggest));
    return 0;
}
#endif  // CONFIG_CONSOLE_COMMANDS_HEAP

#ifdef CONFIG_CONSOLE_COMMANDS_TOP
const char *task_state_name(eTaskState s)
{
    switch (s) {
        case eRunning:   return "run";
        case eReady:     return "ready";
        case eBlocked:   return "block";
        case eSuspended: return "susp";
        case eDeleted:   return "del";
        default:         return "?";
    }
}

int cmd_top(int, char **)
{
    const UBaseType_t count = uxTaskGetNumberOfTasks();
    auto             *tasks = static_cast<TaskStatus_t *>(pvPortMalloc(count * sizeof(TaskStatus_t)));
    if (tasks == nullptr) {
        std::printf("top: out of memory\n");
        return 1;
    }

    uint32_t          total_runtime = 0;
    const UBaseType_t n = uxTaskGetSystemState(tasks, count, &total_runtime);
    const uint32_t    denom = total_runtime ? total_runtime : 1;

    std::printf("%-18s %5s %6s %8s %s\n", "task", "cpu%", "prio", "stackHW", "state");
    for (UBaseType_t i = 0; i < n; ++i) {
        const float cpu = 100.0f * static_cast<float>(tasks[i].ulRunTimeCounter) / denom;
        std::printf("%-18s %4.1f %6u %8u %s\n", tasks[i].pcTaskName, cpu,
                    static_cast<unsigned>(tasks[i].uxCurrentPriority),
                    static_cast<unsigned>(tasks[i].usStackHighWaterMark),
                    task_state_name(tasks[i].eCurrentState));
    }

    vPortFree(tasks);
    return 0;
}
#endif  // CONFIG_CONSOLE_COMMANDS_TOP

#ifdef CONFIG_CONSOLE_COMMANDS_REBOOT
int cmd_reboot(int, char **)
{
    std::printf("rebooting...\n");
    esp_restart();  // does not return
    return 0;
}
#endif  // CONFIG_CONSOLE_COMMANDS_REBOOT

void register_commands()
{
#ifdef CONFIG_CONSOLE_COMMANDS_REBOOT
    const esp_console_cmd_t reboot = {.command = "reboot",
                                      .help    = "restart the device",
                                      .hint    = nullptr,
                                      .func    = &cmd_reboot,
                                      .argtable       = nullptr,
                                      .func_w_context = nullptr,
                                      .context        = nullptr};
    ESP_ERROR_CHECK(esp_console_cmd_register(&reboot));
#endif
#ifdef CONFIG_CONSOLE_COMMANDS_HEAP
    const esp_console_cmd_t heap = {.command = "heap",
                                    .help    = "show heap usage",
                                    .hint    = nullptr,
                                    .func    = &cmd_heap,
                                    .argtable       = nullptr,
                                    .func_w_context = nullptr,
                                    .context        = nullptr};
    ESP_ERROR_CHECK(esp_console_cmd_register(&heap));
#endif
#ifdef CONFIG_CONSOLE_COMMANDS_TOP
    const esp_console_cmd_t top = {.command = "top",
                                   .help    = "list tasks with cpu/stack usage",
                                   .hint    = nullptr,
                                   .func    = &cmd_top,
                                   .argtable       = nullptr,
                                   .func_w_context = nullptr,
                                   .context        = nullptr};
    ESP_ERROR_CHECK(esp_console_cmd_register(&top));
#endif
}

}  // namespace

void ConsoleCommandsInit()
{
#ifdef CONFIG_CONSOLE_COMMANDS_HEAP
    g_heap_at_boot = esp_get_free_heap_size();
#endif

    esp_console_repl_t       *repl = nullptr;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.task_stack_size           = 8192;
    repl_cfg.prompt                    = "withr_knob>";

#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    esp_console_dev_uart_config_t dev = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&dev, &repl_cfg, &repl));
#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
    esp_console_dev_usb_cdc_config_t dev = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&dev, &repl_cfg, &repl));
#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    esp_console_dev_usb_serial_jtag_config_t dev = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&dev, &repl_cfg, &repl));
#else
#error "No supported ESP console device selected in menuconfig"
#endif

    register_commands();
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
