// Stand-in for the ESP32's Preferences (NVS) so storage.cpp can be built and
// tested on a computer. Keeps everything in memory; only the calls storage.cpp
// uses are provided.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>

namespace fake_nvs {

// Everything "in flash", keyed by "namespace/key". Tests may read, change or
// clear it directly.
inline std::map<std::string, std::vector<uint8_t>> store;

}  // namespace fake_nvs

class Preferences {
 public:
  bool begin(const char* name, bool /*readOnly*/ = false) {
    prefix_ = std::string(name) + "/";
    return true;
  }
  void end() {}

  size_t getBytesLength(const char* key) {
    auto it = fake_nvs::store.find(prefix_ + key);
    return it == fake_nvs::store.end() ? 0 : it->second.size();
  }
  size_t getBytes(const char* key, void* buf, size_t maxLen) {
    auto it = fake_nvs::store.find(prefix_ + key);
    if (it == fake_nvs::store.end() || it->second.size() > maxLen) return 0;
    memcpy(buf, it->second.data(), it->second.size());
    return it->second.size();
  }
  size_t putBytes(const char* key, const void* value, size_t len) {
    const uint8_t* bytes = (const uint8_t*)value;
    fake_nvs::store[prefix_ + key].assign(bytes, bytes + len);
    return len;
  }

 private:
  std::string prefix_;
};
