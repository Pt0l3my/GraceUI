// display.cpp: what a mod learns of the display (display.h, D180).
#include "display.h"

#include <atomic>

#include "grace/abi/consumers.h"

namespace grace::host::display {
namespace {

constinit std::atomic<std::uint32_t> g_revision{0};  // raised after every change once warm
constinit std::atomic<bool> g_warm{false};
constinit std::atomic<std::uint32_t> g_width{0};
constinit std::atomic<std::uint32_t> g_height{0};
constinit std::atomic<float> g_ui_scale{0.0f};
constinit std::atomic<float> g_ui_default{0.0f};
constinit std::atomic<float> g_faces_scale{0.0f};
constinit std::atomic<std::uint32_t> g_seen[grace::abi::kMaxConsumers + 1] = {};

void changed() noexcept {
  if (g_warm.load(std::memory_order_acquire)) {
    g_revision.fetch_add(1, std::memory_order_acq_rel);
  }
}

}  // namespace

void set_ui_scale(float value, float default_value) noexcept {
  const bool same = g_ui_scale.load(std::memory_order_relaxed) == value &&
                    g_ui_default.load(std::memory_order_relaxed) == default_value;
  g_ui_scale.store(value, std::memory_order_relaxed);
  g_ui_default.store(default_value, std::memory_order_relaxed);
  if (!same) {
    changed();
  }
}

void set_size(std::uint32_t width, std::uint32_t height) noexcept {
  const bool same = g_width.load(std::memory_order_relaxed) == width && g_height.load(std::memory_order_relaxed) == height;
  g_width.store(width, std::memory_order_relaxed);
  g_height.store(height, std::memory_order_relaxed);
  if (!same) {
    changed();
  }
}

void set_faces_scale(float content_scale) noexcept {
  g_faces_scale.store(content_scale, std::memory_order_relaxed);
  g_warm.store(true, std::memory_order_release);
  g_revision.fetch_add(1, std::memory_order_acq_rel);
}

Snapshot read() noexcept {
  Snapshot out;
  for (int attempt = 0; attempt < 8; ++attempt) {
    const std::uint32_t before = g_revision.load(std::memory_order_acquire);
    out.width = g_width.load(std::memory_order_relaxed);
    out.height = g_height.load(std::memory_order_relaxed);
    out.ui_scale = g_ui_scale.load(std::memory_order_relaxed);
    out.ui_scale_default = g_ui_default.load(std::memory_order_relaxed);
    out.faces_scale = g_faces_scale.load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);
    if (g_revision.load(std::memory_order_relaxed) == before) {
      out.revision = g_warm.load(std::memory_order_acquire) ? before : 0;
      break;
    }
  }
  out.scale = out.height > 0 ? static_cast<float>(out.height) / 1080.0f * out.ui_scale : 0.0f;
  out.content_scale = out.ui_scale_default > 0.0f ? out.scale / out.ui_scale_default : 0.0f;
  return out;
}

float content_scale(std::uint32_t height) noexcept {
  const float value = g_ui_scale.load(std::memory_order_relaxed);
  const float default_value = g_ui_default.load(std::memory_order_relaxed);
  if (height == 0 || value <= 0.0f || default_value <= 0.0f) {
    return 1.0f;
  }
  return static_cast<float>(height) / 1080.0f * value / default_value;
}

void connected(std::uint32_t owner) noexcept {
  if (owner <= grace::abi::kMaxConsumers) {
    g_seen[owner].store(0, std::memory_order_release);
  }
}

bool take(std::uint32_t owner, Snapshot& out) noexcept {
  if (owner > grace::abi::kMaxConsumers) {
    return false;
  }
  const Snapshot now = read();
  std::uint32_t seen = g_seen[owner].load(std::memory_order_acquire);
  if (now.revision == 0 || seen == now.revision ||
      !g_seen[owner].compare_exchange_strong(seen, now.revision, std::memory_order_acq_rel)) {
    return false;  // nothing new, or another of the mod's threads took it
  }
  out = now;
  return true;
}

}  // namespace grace::host::display
