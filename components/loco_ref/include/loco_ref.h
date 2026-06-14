#pragma once
#include <string>

// A reference to a locomotive by DCC address and address length.
struct LocoRef {
    int         address = 0;
    char        length  = 'L';   // 'S' (short) or 'L' (long)
    std::string name;            // display name; may be empty until roster loads
};

// Parse a WiThrottle address token like "S3" or "L1234" into out.
// Returns false if the token is malformed (too short, non-numeric tail).
bool loco_parse_address(const std::string& token, LocoRef& out);

// Format an address+length back into a WiThrottle token ("S3", "L1234").
std::string loco_format_address(int address, char length);
