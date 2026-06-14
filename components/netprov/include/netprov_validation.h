#pragma once
// Pure header — no ESP-IDF includes.
#include <string>
#include <cstdint>

namespace netprov {
bool valid_ipv4(const std::string& s);              // "1.2.3.4", each octet 0..255
bool valid_port(const std::string& s, uint16_t& out); // numeric, 1..65535
bool valid_ssid(const std::string& s);              // length 1..32
bool valid_password(const std::string& s);          // empty (open) OR length 8..63
}
