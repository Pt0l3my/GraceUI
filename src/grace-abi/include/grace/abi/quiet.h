// quiet.h: the mods whose notices the player has quieted (G32, D181), by mod id, and what they posted meanwhile. A quiet
// mod's notice is accepted as any other (the mod learns nothing: invariant 2's spirit) but kept here, not drawn over the
// game; GraceUI's own notices are never quiet. Mods' threads keep their notices; GraceUI's own thread sets the quiet
// mods and reads the kept list. Pure; a mutex guards it (never on the present thread).
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace grace::abi {

inline constexpr std::size_t kQuietMods = 64;   // ids kept as quiet: more than mods connect at once (32)
inline constexpr std::size_t kQuietKept = 32;   // a quiet mod's notices kept, the newest

class QuietNotices {
 public:
  struct Kept {
    std::string id;
    std::string name;  // the mod's display name when it posted
    std::string text;
    std::uint32_t key = 0;     // the mod's key for it, 0 for none
    std::uint64_t serial = 0;  // rises with each notice kept: newer is larger
  };

  // The player's switch for mod `id`: quiet or shown. False, changing nothing, for an id connect would refuse
  // (valid_id), or a new quiet id past kQuietMods.
  bool set(std::string_view id, bool quiet);
  bool quiet(std::string_view id) const;
  // The quiet ids, sorted (what is kept between sessions, D179).
  std::vector<std::string> ids() const;
  // Takes back what ids() gave in an earlier session; an id connect would refuse is left out. Answers how many.
  std::size_t restore(const std::vector<std::string>& ids);

  // A notice of quiet mod `id`, kept rather than drawn: the oldest goes past kQuietKept. A notice with a key the mod
  // kept before replaces that one, as it would on screen, and becomes the newest; key 0 is always a new one. An empty
  // text (a withdrawal) is not kept.
  void keep(std::string_view id, std::string_view name, std::uint32_t key, std::string_view text);
  // The kept notices, newest first.
  std::vector<Kept> kept() const;

  // Rises with every change of the quiet ids or the kept list: a view of them compares it with the last it built.
  std::uint64_t revision() const;
  // Rises with every change of the quiet ids alone: what is kept between sessions compares it with the last it wrote.
  std::uint64_t ids_revision() const;

 private:
  mutable std::mutex lock_;
  std::vector<std::string> ids_;  // sorted
  std::vector<Kept> kept_;        // oldest first
  std::uint64_t serial_ = 0;
  std::uint64_t revision_ = 1;
  std::uint64_t ids_revision_ = 1;
};

}  // namespace grace::abi
