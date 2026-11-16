// consumers.cpp: the mods connected to GraceUI (consumers.h).
#include "grace/abi/consumers.h"

#include <cstring>

#include "grace/abi/text.h"

namespace grace::abi {
namespace {

// A handle: the slot's generation above, its number (1 to kMaxConsumers) in the low byte; never 0.
constexpr GraceUiConsumer handle_of(std::uint32_t slot, std::uint32_t generation) noexcept {
  return (static_cast<GraceUiConsumer>(generation) << 8) | (slot + 1);
}

template <std::size_t N>
void copy_into(char (&out)[N], std::string_view text) noexcept {
  const std::size_t length = text.size() < N - 1 ? text.size() : N - 1;
  if (length != 0) {
    std::memcpy(out, text.data(), length);
  }
  out[length] = '\0';
}

}  // namespace

ConnectResult Consumers::connect(const ConnectRequest& request, std::uint32_t graceui_version,
                                 Inactive inactive) noexcept {
  ConnectResult result;
  if (!valid_id(request.id)) {
    result.reason = GRACEUI_REFUSED_ID;
    return result;
  }
  if (!valid_name(request.name)) {
    result.reason = GRACEUI_REFUSED_NAME;
    return result;
  }
  if (!valid_caps(request.caps)) {
    result.reason = GRACEUI_REFUSED_CAPS;
    return result;
  }
  if (request.floor != 0 && graceui_version < request.floor) {
    result.status = GRACEUI_TOO_OLD;
    return result;
  }
  const SpinGuard guard(lock_);
  std::uint32_t free = kMaxConsumers;
  for (std::uint32_t i = 0; i < kMaxConsumers; ++i) {
    const Slot& slot = slots_[i];
    if (!slot.connected) {
      free = free == kMaxConsumers ? i : free;
      continue;
    }
    if (request.id == std::string_view(slot.info.id)) {
      result.status = GRACEUI_DUPLICATE_ID;
      return result;
    }
  }
  if (free == kMaxConsumers) {
    result.reason = GRACEUI_REFUSED_TOO_MANY;
    return result;
  }
  Slot& slot = slots_[free];
  slot.connected = true;
  ++slot.generation;
  slot.info = ConsumerInfo{};
  slot.info.owner = free + 1;
  slot.info.version = request.version;
  slot.info.floor = request.floor;
  slot.info.abi_minor = request.abi_minor;
  copy_into(slot.info.id, request.id);
  copy_into(slot.info.name, request.name);
  copy_into(slot.info.caps, request.caps);
  result.handle = handle_of(free, slot.generation);
  result.owner = free + 1;
  if (inactive != Inactive::none) {
    result.status = GRACEUI_INACTIVE;
    result.reason = static_cast<std::uint32_t>(inactive);
  } else {
    result.status = GRACEUI_OK;
  }
  return result;
}

GraceUiStatus Consumers::disconnect(GraceUiConsumer handle, std::uint32_t& owner) noexcept {
  const SpinGuard guard(lock_);
  owner = 0;
  const GraceUiConsumer number = handle & 0xFFu;
  if (number == 0 || number > kMaxConsumers) {
    return GRACEUI_NOT_CONNECTED;
  }
  Slot& slot = slots_[number - 1];
  if (!slot.connected || handle_of(static_cast<std::uint32_t>(number - 1), slot.generation) != handle) {
    return GRACEUI_NOT_CONNECTED;
  }
  slot.connected = false;
  owner = static_cast<std::uint32_t>(number);
  return GRACEUI_OK;
}

std::uint32_t Consumers::owner_of(GraceUiConsumer handle) const noexcept {
  const SpinGuard guard(lock_);
  const GraceUiConsumer number = handle & 0xFFu;
  if (number == 0 || number > kMaxConsumers) {
    return 0;
  }
  const Slot& slot = slots_[number - 1];
  return slot.connected && handle_of(static_cast<std::uint32_t>(number - 1), slot.generation) == handle
             ? static_cast<std::uint32_t>(number)
             : 0;
}

bool Consumers::info(std::uint32_t index, ConsumerInfo& out) const noexcept {
  const SpinGuard guard(lock_);
  for (const Slot& slot : slots_) {
    if (slot.connected && index-- == 0) {
      out = slot.info;
      return true;
    }
  }
  return false;
}

bool Consumers::info_of(std::uint32_t owner, ConsumerInfo& out) const noexcept {
  const SpinGuard guard(lock_);
  if (owner == 0 || owner > kMaxConsumers || !slots_[owner - 1].connected) {
    return false;
  }
  out = slots_[owner - 1].info;
  return true;
}

std::uint32_t Consumers::count() const noexcept {
  const SpinGuard guard(lock_);
  std::uint32_t connected = 0;
  for (const Slot& slot : slots_) {
    connected += slot.connected ? 1u : 0u;
  }
  return connected;
}

}  // namespace grace::abi
