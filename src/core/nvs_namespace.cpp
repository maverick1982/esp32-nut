#include "core/nvs_namespace.h"
#include <nvs.h>

namespace NvsNamespace {

bool exists(const char* name) {
    if (!name) return false;
    nvs_iterator_t it = nullptr;
    // Lists the entries without opening the namespace: no error log when it is missing.
    // A namespace whose keys were all removed counts as missing: it has nothing to read.
    esp_err_t err = nvs_entry_find(NVS_DEFAULT_PART_NAME, name, NVS_TYPE_ANY, &it);
    nvs_release_iterator(it);
    return err == ESP_OK;
}

bool beginRead(Preferences& prefs, const char* name) {
    return exists(name) && prefs.begin(name, true);
}

}  // namespace NvsNamespace
