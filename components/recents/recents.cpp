#include "recents.h"
#include "recents_serialize.h"
#include "Param.h"

static params::Param<std::string>& blob() {
    static params::Param<std::string> p{"recents", std::string("")};
    return p;
}

std::vector<LocoRef> recents_load() {
    return recents_deserialize(blob().get());
}

void recents_store_push(const LocoRef& r) {
    std::vector<LocoRef> list = recents_deserialize(blob().get());
    recents_push(list, r, RECENTS_MAX);
    blob().set(recents_serialize(list));   // set() persists to NVS
}
