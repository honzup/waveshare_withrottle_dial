#pragma once
#include <vector>
#include <string>

// Return the index of the first function name containing "horn"
// (case-insensitive), or 2 if none is found. Mirrors m5's resolve_horn_fn.
int resolve_horn_fn(const std::vector<std::string>& names);
