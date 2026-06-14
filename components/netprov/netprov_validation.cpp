#include "netprov_validation.h"
#include <cstdlib>

namespace netprov {

bool valid_ipv4(const std::string& s) {
    int octets = 0;
    size_t i = 0;
    while (i < s.size()) {
        size_t start = i;
        int val = 0, digits = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            val = val * 10 + (s[i] - '0');
            ++digits; ++i;
            if (val > 255) return false;
        }
        if (digits == 0 || i == start) return false;
        ++octets;
        if (i < s.size()) {
            if (s[i] != '.') return false;
            ++i;
            if (i == s.size()) return false; // trailing dot
        }
    }
    return octets == 4;
}

bool valid_port(const std::string& s, uint16_t& out) {
    if (s.empty()) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    long v = std::strtol(s.c_str(), nullptr, 10);
    if (v < 1 || v > 65535) return false;
    out = (uint16_t) v;
    return true;
}

bool valid_ssid(const std::string& s)     { return !s.empty() && s.size() <= 32; }
bool valid_password(const std::string& s) { return s.empty() || (s.size() >= 8 && s.size() <= 63); }

} // namespace netprov
