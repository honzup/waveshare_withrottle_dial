#include "horn_resolver.h"
#include <algorithm>
#include <cctype>

int resolve_horn_fn(const std::vector<std::string>& names) {
    for (size_t i = 0; i < names.size(); ++i) {
        std::string lower = names[i];
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        if (lower.find("horn") != std::string::npos)
            return static_cast<int>(i);
    }
    return 2;
}
