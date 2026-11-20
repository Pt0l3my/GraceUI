// quiet.cpp: the mods whose notices the player has quieted (quiet.h, G32, D181).
#include "grace/abi/quiet.h"

#include <algorithm>
#include <utility>

#include "grace/abi/text.h"

namespace grace::abi {

bool QuietNotices::set(std::string_view id, bool quiet) {
  if (!valid_id(id)) {
    return false;
  }
  const std::lock_guard<std::mutex> guard(lock_);
  const auto at = std::lower_bound(ids_.begin(), ids_.end(), id);
  const bool present = at != ids_.end() && *at == id;
  if (quiet == present) {
    return true;  // as it was: nothing changes
  }
  if (quiet) {
    if (ids_.size() >= kQuietMods) {
      return false;
    }
    ids_.insert(at, std::string(id));
  } else {
    ids_.erase(at);
  }
  ++revision_;
  ++ids_revision_;
  return true;
}

bool QuietNotices::quiet(std::string_view id) const {
  const std::lock_guard<std::mutex> guard(lock_);
  return std::binary_search(ids_.begin(), ids_.end(), id);
}

std::vector<std::string> QuietNotices::ids() const {
  const std::lock_guard<std::mutex> guard(lock_);
  return ids_;
}

std::size_t QuietNotices::restore(const std::vector<std::string>& ids) {
  std::size_t taken = 0;
  for (const std::string& id : ids) {
    taken += set(id, true) ? 1 : 0;
  }
  return taken;
}

void QuietNotices::keep(std::string_view id, std::string_view name, std::uint32_t key, std::string_view text) {
  if (text.empty()) {
    return;  // a withdrawal: nothing to keep
  }
  Kept made{std::string(id), std::string(name), std::string(text), key, 0};
  const std::lock_guard<std::mutex> guard(lock_);
  if (key != 0) {
    const auto same = std::find_if(kept_.begin(), kept_.end(), [&](const Kept& k) { return k.key == key && k.id == id; });
    if (same != kept_.end()) {
      kept_.erase(same);  // replaced, as on screen
    }
  }
  if (kept_.size() >= kQuietKept) {
    kept_.erase(kept_.begin());
  }
  made.serial = ++serial_;
  kept_.push_back(std::move(made));
  ++revision_;
}

std::vector<QuietNotices::Kept> QuietNotices::kept() const {
  const std::lock_guard<std::mutex> guard(lock_);
  return std::vector<Kept>(kept_.rbegin(), kept_.rend());
}

std::uint64_t QuietNotices::revision() const {
  const std::lock_guard<std::mutex> guard(lock_);
  return revision_;
}

std::uint64_t QuietNotices::ids_revision() const {
  const std::lock_guard<std::mutex> guard(lock_);
  return ids_revision_;
}

}  // namespace grace::abi
