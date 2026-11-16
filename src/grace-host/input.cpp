// input.cpp: the input holder's stub (input.h).
#include "input.h"

#include <atomic>

namespace grace::host::input {
namespace {

constinit std::atomic<Holder> g_holder{Holder::none};

}  // namespace

Holder holder() noexcept { return g_holder.load(std::memory_order_acquire); }

void before_imgui_frame() noexcept {}

}  // namespace grace::host::input
