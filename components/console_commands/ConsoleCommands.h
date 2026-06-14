/*
 * ConsoleCommands.h — start the serial REPL and register debug commands.
 *
 * Independent reimplementation for waveshare_withrottle_dial. No source from the
 * upstream withrottle_dial project is reused here.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John (honzup)
 */
#ifndef WD_CONSOLE_COMMANDS_H
#define WD_CONSOLE_COMMANDS_H

// Bring up the interactive console on the active UART/USB device and register
// the built-in debug commands (reboot / heap / top) selected via Kconfig.
void ConsoleCommandsInit();

#endif  // WD_CONSOLE_COMMANDS_H
