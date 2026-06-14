#pragma once
#include "loco_ref.h"
#include <vector>

constexpr size_t RECENTS_MAX = 10;

// Load recents from NVS (param "recents"). Safe to call repeatedly.
std::vector<LocoRef> recents_load();

// Push a loco to the front (dedup + trim) and persist to NVS.
void recents_store_push(const LocoRef& r);
