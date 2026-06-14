#include "jmri_discovery.h"

namespace jmri_discovery {

ChooseResult choose(const std::vector<JmriServer>& found,
                    const std::string& saved_ip, uint16_t saved_port) {
    if (found.empty()) {
        if (saved_ip.empty()) return { ChooseKind::None, {} };
        return { ChooseKind::UseSaved, { "", saved_ip, saved_port } };
    }
    if (found.size() == 1) return { ChooseKind::UseDiscovered, found.front() };
    // Several found: if the user already picked one (its IP is saved and matches
    // a discovered server), honour that choice; otherwise ask the user to pick.
    if (!saved_ip.empty()) {
        for (const auto& s : found)
            if (s.ip == saved_ip) return { ChooseKind::UseDiscovered, s };
    }
    return { ChooseKind::Ambiguous, {} };
}

} // namespace jmri_discovery
