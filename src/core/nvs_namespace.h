#ifndef NVS_NAMESPACE_H
#define NVS_NAMESPACE_H

#include <Preferences.h>

// Read-only access to an NVS namespace that may not exist yet (issue #136).
// Preferences::begin(name, true) on a namespace never written prints
// "[E][Preferences.cpp:47] begin(): nvs_open failed: NOT_FOUND": on a fresh board one
// line per settings store, which reads as a fault although it only means "nothing saved".
namespace NvsNamespace {

// true when the namespace holds at least one key in the default NVS partition
bool exists(const char* name);

// Opens the namespace read-only, without the error line when it does not exist.
// false: nothing saved (or NVS not available), the caller keeps its defaults.
bool beginRead(Preferences& prefs, const char* name);

}  // namespace NvsNamespace

#endif  // NVS_NAMESPACE_H
