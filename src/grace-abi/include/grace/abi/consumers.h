// consumers.h: the mods connected to GraceUI (D73, D131): what connect answers, the handles every later slot takes, and
// what GraceUI keeps of each mod for its log and its Compatibility page. Pure: the host hands it validated copies of a
// mod's strings (the DLL's abi files copy them out of the mod's memory, D110) and the session's state; this holds the
// rules. Any of the mods' threads may call it at any time; the present thread never does.
#pragma once

#include <cstdint>
#include <string_view>

#include "grace/abi/spin.h"
#include "graceui.h"

namespace grace::abi {

inline constexpr std::uint32_t kMaxConsumers = GRACEUI_MODS_MAX;

// Why GraceUI is inactive this session, if it is (GRACEUI_INACTIVE_*): the host's state when the call is made.
enum class Inactive : std::uint32_t {
  none = 0,
  anti_cheat = GRACEUI_INACTIVE_ANTI_CHEAT,
  host_failed = GRACEUI_INACTIVE_HOST_FAILED,
  render_dead = GRACEUI_INACTIVE_RENDER_DEAD,
};

// A connect request whose strings are GraceUI's own copies, not yet checked.
struct ConnectRequest {
  std::uint32_t abi_minor = 0;
  std::uint32_t version = 0;
  std::uint32_t floor = 0;
  std::string_view id;
  std::string_view name;
  std::string_view caps;
};

struct ConnectResult {
  GraceUiStatus status = GRACEUI_REFUSED;
  std::uint32_t reason = 0;           // GRACEUI_INACTIVE_* or GRACEUI_REFUSED_*
  GraceUiConsumer handle = 0;         // with GRACEUI_OK or GRACEUI_INACTIVE
  std::uint32_t owner = 0;            // the mod's number, 1 to kMaxConsumers: what notices and the holder name it by
};

// What GraceUI keeps of a connected mod.
struct ConsumerInfo {
  std::uint32_t owner = 0;
  std::uint32_t version = 0;
  std::uint32_t floor = 0;
  std::uint32_t abi_minor = 0;
  char id[GRACEUI_ID_MAX + 1] = {};
  char name[GRACEUI_NAME_MAX + 1] = {};
  char caps[GRACEUI_CAPS_MAX + 1] = {};
};

class Consumers {
 public:
  constexpr Consumers() = default;
  Consumers(const Consumers&) = delete;
  Consumers& operator=(const Consumers&) = delete;

  // connect's answer, checked in this order: the strings (refused, with the reason), the floor against
  // `graceui_version` (too_old), the id among the mods connected (duplicate_id), room (refused: too many); then the mod
  // is connected, and the answer is ok, or inactive with `inactive`'s reason. A floor of 0 means any version.
  ConnectResult connect(const ConnectRequest& request, std::uint32_t graceui_version, Inactive inactive) noexcept;

  // Disconnects the handle's mod: GRACEUI_OK, with its number in `owner`, or GRACEUI_NOT_CONNECTED.
  GraceUiStatus disconnect(GraceUiConsumer handle, std::uint32_t& owner) noexcept;

  // The connected mod's number for `handle`, or 0: a handle from before a disconnect never names the slot's next mod.
  std::uint32_t owner_of(GraceUiConsumer handle) const noexcept;

  // The `index`-th mod connected, in the order of their slots; false past the last.
  bool info(std::uint32_t index, ConsumerInfo& out) const noexcept;
  // The mod numbered `owner`; false if none is connected under it.
  bool info_of(std::uint32_t owner, ConsumerInfo& out) const noexcept;

  std::uint32_t count() const noexcept;

 private:
  struct Slot {
    bool connected = false;
    std::uint32_t generation = 0;  // connects in this slot so far: a handle carries it
    ConsumerInfo info{};
  };

  mutable SpinLock lock_;
  Slot slots_[kMaxConsumers]{};
};

}  // namespace grace::abi
