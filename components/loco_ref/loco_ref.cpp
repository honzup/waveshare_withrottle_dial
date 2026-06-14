#include "loco_ref.h"
#include <cctype>
#include <cstdlib>

bool loco_parse_address(const std::string& token, LocoRef& out) {
    if (token.size() < 2) return false;
    char len = token[0];
    if (len != 'S' && len != 'L') return false;
    for (size_t i = 1; i < token.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(token[i]))) return false;
    out.length  = len;
    out.address = std::atoi(token.c_str() + 1);
    return true;
}

std::string loco_format_address(int address, char length) {
    return std::string(1, length) + std::to_string(address);
}
