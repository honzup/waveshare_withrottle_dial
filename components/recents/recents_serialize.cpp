#include "recents_serialize.h"
#include <sstream>

std::string recents_sanitize_name(const std::string& name) {
    std::string out = name;
    for (char& c : out) if (c == ':' || c == ';') c = ' ';
    return out;
}

std::string recents_serialize(const std::vector<LocoRef>& list) {
    std::string out;
    for (size_t i = 0; i < list.size(); ++i) {
        if (i) out += ';';
        out += loco_format_address(list[i].address, list[i].length);
        out += ':';
        out += list[i].name;
    }
    return out;
}

std::vector<LocoRef> recents_deserialize(const std::string& blob) {
    std::vector<LocoRef> out;
    std::stringstream ss(blob);
    std::string entry;
    while (std::getline(ss, entry, ';')) {
        if (entry.empty()) continue;
        size_t colon = entry.find(':');
        std::string token = (colon == std::string::npos) ? entry : entry.substr(0, colon);
        std::string name  = (colon == std::string::npos) ? ""    : entry.substr(colon + 1);
        LocoRef r;
        if (!loco_parse_address(token, r)) continue;
        r.name = name;
        out.push_back(r);
    }
    return out;
}

void recents_push(std::vector<LocoRef>& list, const LocoRef& entry, size_t max_entries) {
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].address == entry.address && list[i].length == entry.length) {
            list.erase(list.begin() + i);
            break;
        }
    }
    LocoRef e = entry;
    e.name = recents_sanitize_name(entry.name);
    list.insert(list.begin(), e);
    if (list.size() > max_entries) list.resize(max_entries);
}
