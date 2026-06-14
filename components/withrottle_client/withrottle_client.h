/*
 * withrottle_client.h — public interface to the background WiThrottle client.
 *
 * The UI task calls these from any context; the client task owns the actual
 * protocol session. Getters return cached state and never block on the network.
 *
 * Independently authored for waveshare_withrottle_dial. The WiThrottleProtocol
 * types referenced here (Direction, MAX_FUNCTIONS, ...) belong to the
 * third-party library in components/withrottle.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John (honzup)
 */
#pragma once

#include <stdbool.h>
#include <optional>
#include <string>
#include <vector>
#include "WiThrottleProtocol.h"
#include "loco_ref.h"
#include "jmri_discovery.h"

namespace withr
{

// Where we are in the bring-up sequence, for the "connecting" screen.
enum class Phase { WIFI, SERVER, ROSTER, READY };
Phase get_phase();
bool  roster_ready();           // true once the first roster entry has arrived

// Roster snapshot for the UI, sorted by name (case-insensitive).
std::vector<LocoRef> get_roster();

// Loco selection. acquire_loco()/release_loco() are async (queued to the client
// task). want_loco() reports whether a loco is *selected* — it stays true across
// a disconnect so the loco is re-acquired on reconnect and the UI returns to the
// throttle screen rather than the menu. has_loco() reports whether one is
// actually acquired on the server right now.
void acquire_loco(int address, char length);
void release_loco();
bool has_loco();
bool want_loco();

// Horn: get_horn_fn() is the function index resolved for the active loco
// (default 2); horn() is shorthand for set_function(get_horn_fn(), on).
int  get_horn_fn();
void horn(bool on);

// Start the client task. Call once after Wi-Fi has been initialised.
void client_start(void);

// Set speed as a percentage in [0, 100]. Queued; safe from the UI task.
void set_speed(uint8_t percent);

// Current speed as a percentage in [0, 100], or nullopt when not connected.
std::optional<uint8_t> get_speed();

// Current direction, or nullopt when not connected.
std::optional<Direction> get_direction();

// Queue an emergency stop.
void emergency_stop(void);

// Set the direction (queued).
void set_direction(Direction dir);

// True when connected with a loco acquired.
bool is_connected(void);

// Name of the loco on the throttle, or nullopt if none.
std::optional<std::string> get_loco_name();

// Cached on/off state of function `func`, or nullopt if not connected / out of
// range.
std::optional<bool> get_function_state(uint8_t func);

// Name of function `func`. nullopt if not connected; an empty string means the
// function is undefined for this loco.
std::optional<std::string> get_function_name(uint8_t func);

// Queue a function on/off command.
void set_function(uint8_t func, bool state);

// The "ip:port" the client is using (or trying) for the WiThrottle server.
std::string get_server_url();

// Candidate servers awaiting a choice when mDNS found several JMRI instances.
std::vector<jmri_discovery::JmriServer> get_server_choices();

// Persist a chosen JMRI server (writes the jmri_ip / jmri_port params to NVS).
void set_jmri_server(const std::string& ip, uint16_t port);

} // namespace withr
