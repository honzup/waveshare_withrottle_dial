#pragma once
// Pure header — MUST NOT include any ESP-IDF headers (compiled on native too).
#include <string>
#include <vector>
#include <cstdint>

namespace jmri_discovery {

struct JmriServer {
    std::string name;   // mDNS instance name (may be empty)
    std::string ip;     // dotted IPv4
    uint16_t    port;   // WiThrottle port
};

// Outcome of reconciling discovered servers against the saved fallback.
enum class ChooseKind {
    None,          // nothing discovered and no usable saved IP — keep waiting
    UseSaved,      // nothing discovered; fall back to the saved IP/port
    UseDiscovered, // exactly one discovered — use it (and the caller remembers it)
    Ambiguous      // several discovered — caller must let the user pick
};

struct ChooseResult {
    ChooseKind kind;
    JmriServer server; // valid for UseSaved and UseDiscovered
};

// Pure: decide which server to dial. `found` is the mDNS result set.
ChooseResult choose(const std::vector<JmriServer>& found,
                    const std::string& saved_ip, uint16_t saved_port);

// IDF-only (defined in jmri_discovery.cpp): browse _withrottle._tcp.
std::vector<JmriServer> discover(uint32_t timeout_ms);

} // namespace jmri_discovery
