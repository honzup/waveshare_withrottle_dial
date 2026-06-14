#pragma once
#include <string>
#include <vector>
#include "loco_ref.h"

// Serialise recents to "S3:Name;L1234:Name". Names are emitted verbatim;
// callers must strip ':' and ';' before pushing (see recents_sanitize_name).
std::string recents_serialize(const std::vector<LocoRef>& list);

// Parse the serialised form. Malformed entries are skipped.
std::vector<LocoRef> recents_deserialize(const std::string& blob);

// Insert entry at the front, dedup by (address,length), trim to max_entries.
// If an existing entry matches, it is moved to the front and its name updated.
void recents_push(std::vector<LocoRef>& list, const LocoRef& entry, size_t max_entries);

// Replace ':' and ';' with spaces so a name is safe to serialise.
std::string recents_sanitize_name(const std::string& name);
