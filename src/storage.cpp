#include "storage.h"

#include <Preferences.h>

#include "session.h"

namespace storage {

namespace {

const char* const kNamespace = "m5drums";
const char* const kKey = "session";

}  // namespace

bool load(Sequencer& seq) {
  uint8_t bytes[session::kBytes];
  Preferences prefs;
  // Opened read-write: a read-only open logs an error on first boot, when the
  // namespace does not exist yet.
  if (!prefs.begin(kNamespace, false)) return false;
  bool ok = prefs.getBytesLength(kKey) == sizeof(bytes) &&
            prefs.getBytes(kKey, bytes, sizeof(bytes)) == sizeof(bytes);
  prefs.end();
  // A save from another version of the session layout is refused here.
  return ok && session::unpack(bytes, sizeof(bytes), seq);
}

void save(const Sequencer& seq) {
  uint8_t bytes[session::kBytes];
  session::pack(seq, bytes);

  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return;
  prefs.putBytes(kKey, bytes, sizeof(bytes));
  prefs.end();
}

}  // namespace storage
