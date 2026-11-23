// draw.cpp: the render path (D107; invariants 10 and 11). On the first Present of the bound swap chain once its present
// queue is known, warm-up asks GraceUI's own thread for Dear ImGui's context and the font atlas, baked with every face
// registered so far (D117, D139 as amended), and draws nothing until it is done; then this thread makes Dear ImGui's
// Win32 backend for the swap chain's window and its DX12 backend for the game's device and that queue, and uploads the
// atlas once, with every mod's texture registered so far. Then, on every frame with anything to draw, one command list draws the
// frame onto the current back buffer, layer by layer (D76: the mods' hud and panel layers, GraceUI's own drawing, the
// modal layer, the notices), and is executed on the present queue before the original Present. A texture a later mod
// registers is uploaded at the frame after, on the same queue, and drawn once its copy's fence has passed. It never
// waits for the GPU: a frame slot still in use is skipped and counted (D119). After warm-up it allocates nothing per
// frame and frees nothing: each panel's newest draw list is drawn where it lies, in its triple buffer (D137).
//
// SAFETY: the raw pointers here follow three rules. (1) Every COM object this file creates or is handed with a
// reference (the device, the queue, its own heaps, fence, allocators, lists and textures) is owned by one field and
// released once, through release() below. (2) The swap chain and its back buffers are not owned (D118): hooks.cpp binds
// a swap chain only with a live tag and calls unbind() once the tag says the swap chain has ended, and
// mark_buffers_stale() before ResizeBuffers replaces the buffers, which render() takes before it touches one, so no
// pointer into them is used after them. (3) Dear ImGui's objects (context, draw data, atlas texture) are used only
// between their creation and the next call that may replace them, on this thread; the context and the atlas, and the
// atlas's texture and upload buffer, are made by GraceUI's own thread before it stores `done` (g_bake), which this
// thread loads before it touches any of them, and GraceUI's thread never touches them again. The notices it draws are read from
// the session's newest list (session.h), which no writer touches while this thread holds it. (4) A panel's draw list is
// the reader's buffer of its triple buffer (drawlist.h), which no writer touches until this thread's next read of that
// panel: this thread moves its vertices in place to where the panel lies, and lends its vertex and index arrays to an
// ImDrawList of its own for the one RenderDrawData call that copies them, taking them back before it returns; the
// vertices have Dear ImGui's ImDrawVert layout (the static_asserts below) and the indices its ImDrawIdx.
#include "draw.h"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "counters.h"
#include "display.h"
#include "draw_alloc.h"
#include "draw_atlas.h"
#include "grace/abi/panels.h"
#include "grace/abi/registry.h"
#include "grace/draw/pointer.h"
#include "grace/tokens.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#include "imgui_internal.h"  // ImFontAtlasBuildMain: the atlas baked once, before the first frame (D117)
#include "hooks.h"
#include "input.h"
#include "log.h"
#include "menu.h"
#include "session.h"

#if defined(GRACEUI_TEST_EXPORTS)
#include "graceui_test.h"
#endif

namespace grace::host {

// Dear ImGui's failed assertions end here (grace_imconfig.h, D121); hooks.cpp catches the exception at the detour.
[[noreturn]] void imgui_assert_failed(const char* expression, int line) { throw ImGuiAssertion{expression, line}; }

namespace draw {
namespace {

constexpr UINT kSlots = 4;  // frames GraceUI may have on the GPU at once (EST, D119), as many as the DX12 backend's
constexpr UINT kMaxBuffers = DXGI_MAX_SWAP_CHAIN_BUFFERS;
// The shader-visible heap: the atlas, then one descriptor for each texture slot (1 + its slot), then the DX12 backend's.
constexpr UINT kTextureDescriptors = grace::abi::kMaxTextures;
constexpr UINT kSrvDescriptors = 1 + kTextureDescriptors + 16;

// A panel's vertices and indices are drawn as they lie (SAFETY (4)): GraceUI's vertex is Dear ImGui's.
static_assert(sizeof(GraceUiDrawVert) == sizeof(ImDrawVert) && offsetof(GraceUiDrawVert, x) == offsetof(ImDrawVert, pos) &&
              offsetof(GraceUiDrawVert, u) == offsetof(ImDrawVert, uv) &&
              offsetof(GraceUiDrawVert, colour) == offsetof(ImDrawVert, col));
static_assert(std::is_same_v<ImDrawIdx, std::uint16_t>, "D76's u16 indices are Dear ImGui's default ImDrawIdx");

// Notices (D134, §3.8): Grace's toasts at the top right, oldest first, above everything: a ground.2 card with a rule
// border and 4 px corners, a 3 px accent in gold, ember or rot (ember and rot with their icon, D25), and ink.hi text in
// Noto Sans 15 on 22 px lines (Dear ImGui's own font where GraceUI's fonts folder lacks it), at most 3 lines.
constexpr float kNoticeMargin = 12.0f;
constexpr float kNoticeWidth = 300.0f;
constexpr float kNoticeAccent = 3.0f;
constexpr float kNoticePadX = 12.0f;
constexpr float kNoticePadY = 8.0f;
constexpr float kNoticeGap = 6.0f;
constexpr float kNoticeRadius = grace::tokens::radius::card;
constexpr float kNoticeIcon = 12.0f;
constexpr float kNoticeIconGap = 6.0f;
constexpr int kNoticeLines = 3;
constexpr std::uint32_t kNoticesDrawn = 5;

constexpr ImU32 opaque(std::uint32_t rgb) noexcept {
  return (0xFFu << IM_COL32_A_SHIFT) | (((rgb >> 16) & 0xFFu) << IM_COL32_R_SHIFT) |
         (((rgb >> 8) & 0xFFu) << IM_COL32_G_SHIFT) | ((rgb & 0xFFu) << IM_COL32_B_SHIFT);
}
// The Grace theme's tokens (tokens/grace.toml, D157): ground.2, ink.hi, the three accents, and rule over ground.2.
constexpr const grace::tokens::Palette& kTokens = grace::tokens::kGrace.colour;
constexpr std::uint32_t kGround2 = kTokens.ground_2.rgb();
constexpr std::uint32_t kInkHigh = kTokens.ink_hi.rgb();
constexpr std::uint32_t kGold = kTokens.gold.rgb();
constexpr std::uint32_t kEmber = kTokens.ember.rgb();
constexpr std::uint32_t kRot = kTokens.rot.rgb();
constexpr std::uint32_t kRule = grace::tokens::over(kTokens.rule, kTokens.ground_2).rgb();
constexpr ImU32 kAccents[] = {opaque(kGold), opaque(kEmber), opaque(kRot)};  // by GRACEUI_TONE_*

#if defined(GRACEUI_TEST_EXPORTS)
static_assert(kNoticeMargin == GRACEUI_TEST_NOTICE_MARGIN && kNoticeWidth == GRACEUI_TEST_NOTICE_WIDTH &&
              kNoticeAccent == GRACEUI_TEST_NOTICE_ACCENT && kGround2 == GRACEUI_TEST_NOTICE_GROUND &&
              kGold == GRACEUI_TEST_NOTICE_GOLD && kEmber == GRACEUI_TEST_NOTICE_EMBER &&
              kRot == GRACEUI_TEST_NOTICE_ROT && kNoticePadY == GRACEUI_TEST_NOTICE_PAD_Y &&
              atlas::kNoticeLineHeight == GRACEUI_TEST_NOTICE_LINE,
              "graceui_test.h tells the harness where notices are drawn, and in what colours");
#endif

// The foreground draw list's room, reserved at warm-up for the most one frame draws: five notices of three lines (Dear
// ImGui reserves four vertices and six indices for every byte of a text it draws, then gives back what it did not
// use), the test panel and a holder's pointer (D178).
constexpr int kReservedVertices = 8192;
constexpr int kReservedIndices = 12288;
constexpr int kFrameLists = static_cast<int>(grace::abi::kMaxPanels) + 32;  // the frame's draw lists, all layers

template <class T>
void release(T*& object) noexcept {
  if (object != nullptr) {
    object->Release();
    object = nullptr;
  }
}

struct Slot {
  ID3D12CommandAllocator* allocator;
  ID3D12GraphicsCommandList* list;
  UINT64 fence_value;  // the fence value signalled after this slot's last list: reusable once the fence reaches it
};

struct BackBuffer {
  ID3D12Resource* resource;  // not owned (D118)
  D3D12_CPU_DESCRIPTOR_HANDLE rtv;
};

// A mod's texture on the GPU, by its slot in the registry (registry.h): the registry holds its state, this its objects.
struct GpuTexture {
  ID3D12Resource* resource;
  ID3D12Resource* upload;  // released once its copy's fence has passed
  UINT64 copied_after;     // the fence value after its copy
  UINT64 release_after;    // once its mod has gone: the fence value after the last frame that could draw it
  bool releasing;
};

struct Renderer {
  // The bound swap chain, not owned (D118).
  IDXGISwapChain3* swap_chain;
  HWND window;
  UINT width;
  UINT height;
  UINT buffer_count;
  DXGI_FORMAT format;

  ID3D12Device* device;              // one reference
  ID3D12CommandQueue* queue;         // one reference, once found (D116)
  ID3D12CommandQueue* retired_queue;  // a former queue, released once the fence passes retired_after
  UINT64 retired_after;

  bool ready;           // our objects, the context and both backends exist, and the atlas's upload is queued
  bool format_pending;  // the swap chain's format changed: rebuild the DX12 backend once our frames are done
  bool off;
  bool primed;          // warm-up's last frame, built and never submitted, has run (prime_frame)
  bool dx12_live;       // the DX12 backend is initialised on the device (released at the teardown, L3)
  bool teardown;        // the device was removed or changed: everything made on it is let go once no frame needs it

  ID3D12DescriptorHeap* rtv_heap;
  ID3D12DescriptorHeap* srv_heap;
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_start;
  D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu_start;
  D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu_start;
  UINT rtv_increment;
  UINT srv_increment;
  UINT srv_used;

  ID3D12Fence* fence;
  UINT64 fence_last;
  Slot slots[kSlots];
  std::uint64_t draws;  // RenderDrawData calls since the backend's last Init: its frame index, plus one (D119)
  BackBuffer buffers[kMaxBuffers];

  ID3D12Resource* atlas;
  ID3D12Resource* atlas_upload;
  ID3D12CommandAllocator* upload_allocator;  // the atlas's upload, then each later batch of textures
  ID3D12GraphicsCommandList* upload_list;
  UINT64 upload_fence;                       // the fence value after the last batch: the list is free once it passed
  UINT64 atlas_fence;
  bool atlas_ready;
  D3D12_GPU_DESCRIPTOR_HANDLE atlas_gpu;
  GpuTexture textures[grace::abi::kMaxTextures];

  std::int64_t qpc_frequency;
  std::int64_t qpc_last;
};

constinit Renderer g{};

// Warm-up's own: one ImDrawList for each panel slot, which lends itself a panel's newest list each frame, and the
// frame's draw data, which orders every list by layer. Made once, never freed.
constinit ImDrawList* g_panel_lists[grace::abi::kMaxPanels] = {};
constinit ImDrawList* g_menu_list = nullptr;  // the menu's (D167), lent its list as a panel's is
constinit ImDrawData* g_frame = nullptr;

// Set by the resize detours, from any thread, before ResizeBuffers replaces the bound swap chain's buffers; the render
// path takes it before it draws (mark_buffers_stale).
constinit std::atomic<bool> g_buffers_stale{false};

std::int64_t qpc_now() noexcept {
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  return now.QuadPart;
}

bool fence_passed(UINT64 value) noexcept { return g.fence != nullptr && g.fence->GetCompletedValue() >= value; }

void forget_back_buffers() noexcept {
  for (BackBuffer& buffer : g.buffers) {
    buffer.resource = nullptr;
  }
}

// A present queue that is replaced is kept until GraceUI's last work on it has passed, and nothing of GraceUI's goes to
// the new queue meanwhile (render() waits, L11): one fence serves every queue (D119), and Signal sets its value rather
// than raising it, so work on two queues at once could make it read a later value before an earlier one.
void retire_queue() noexcept {
  if (g.queue == nullptr) {
    return;
  }
  if (g.retired_queue != nullptr) {
    release(g.queue);  // nothing of GraceUI's ran on it: render() has not drawn since the older one was retired
  } else {
    g.retired_queue = g.queue;
    g.retired_after = g.fence_last;
    g.queue = nullptr;
  }
  state::queue_method.store(QueueMethod::none, std::memory_order_relaxed);
}

void release_retired_queue_when_done() noexcept {
  // Before the renderer exists (no fence) nothing of GraceUI's ran on any queue.
  if (g.retired_queue != nullptr && (g.fence == nullptr || fence_passed(g.retired_after))) {
    release(g.retired_queue);
  }
}

const char* method_name(QueueMethod method) noexcept {
  switch (method) {
    case QueueMethod::swap_chain: return "the swap chain named it";
    case QueueMethod::resize1: return "the game named it in ResizeBuffers1";
    case QueueMethod::present: return "it executed inside the original Present";
    case QueueMethod::only_direct: return "the device's only direct queue seen executing";
    case QueueMethod::none: break;
  }
  return "none";
}

// The DX12 backend's descriptor callbacks. The backend calls them only from its own texture path, which GraceUI never
// runs (D117), but its Init requires them; they hand out the shader-visible heap's descriptors after the atlas's.
void srv_alloc(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
  IM_ASSERT(g.srv_used < kSrvDescriptors && "GraceUI's shader-visible heap is full");
  cpu->ptr = g.srv_cpu_start.ptr + static_cast<SIZE_T>(g.srv_used) * g.srv_increment;
  gpu->ptr = g.srv_gpu_start.ptr + static_cast<UINT64>(g.srv_used) * g.srv_increment;
  ++g.srv_used;
}

void srv_free(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {}

bool init_dx12_backend() {
  ImGui_ImplDX12_InitInfo info;
  info.Device = g.device;
  info.CommandQueue = g.queue;  // used only by the backend's own texture path, which never runs (D117)
  info.NumFramesInFlight = static_cast<int>(kSlots);
  info.RTVFormat = g.format;
  info.DSVFormat = DXGI_FORMAT_UNKNOWN;
  info.SrvDescriptorHeap = g.srv_heap;
  info.SrvDescriptorAllocFn = &srv_alloc;
  info.SrvDescriptorFreeFn = &srv_free;
  if (!ImGui_ImplDX12_Init(&info)) {
    return false;
  }
  g.dx12_live = true;
  // D117: the static atlas. Without this flag Dear ImGui bakes every glyph range at the build below and locks the atlas
  // during every frame (no new glyph, no new size: the nearest bake is scaled), so no texture changes during play.
  ImGui::GetIO().BackendFlags &= ~ImGuiBackendFlags_RendererHasTextures;
  // Root signatures, shaders and pipeline states now, at warm-up, rather than in a later frame's NewFrame.
  if (!ImGui_ImplDX12_CreateDeviceObjects()) {
    return false;
  }
  g.draws = 0;
  return true;
}

void transition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter = after;
  list->ResourceBarrier(1, &barrier);
}

// ---- Mods' textures (D76, invariant 11): each uploaded once, on the present queue, in a batch the upload list records;
// drawn once its copy's fence has passed; released, once its mod has gone, after the last frame that could draw it.

D3D12_GPU_DESCRIPTOR_HANDLE texture_gpu(std::uint32_t slot) noexcept {
  D3D12_GPU_DESCRIPTOR_HANDLE handle = g.srv_gpu_start;
  handle.ptr += static_cast<UINT64>(1 + slot) * g.srv_increment;
  return handle;
}

// One texture's objects and copy, recorded into the upload list; false if the device refused an object.
bool record_texture(std::uint32_t slot, const grace::abi::TexturePixels& pixels) {
  GpuTexture& gpu = g.textures[slot];
  D3D12_HEAP_PROPERTIES device_heap{};
  device_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = pixels.width;
  desc.Height = pixels.height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  if (FAILED(g.device->CreateCommittedResource(&device_heap, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&gpu.resource)))) {
    return false;
  }
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT64 upload_bytes = 0;
  g.device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &upload_bytes);
  D3D12_HEAP_PROPERTIES upload_heap{};
  upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC buffer_desc{};
  buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer_desc.Width = upload_bytes;
  buffer_desc.Height = 1;
  buffer_desc.DepthOrArraySize = 1;
  buffer_desc.MipLevels = 1;
  buffer_desc.Format = DXGI_FORMAT_UNKNOWN;
  buffer_desc.SampleDesc.Count = 1;
  buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  void* mapped = nullptr;
  const D3D12_RANGE nothing_read{0, 0};
  if (FAILED(g.device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gpu.upload))) ||
      FAILED(gpu.upload->Map(0, &nothing_read, &mapped))) {
    release(gpu.upload);
    release(gpu.resource);
    return false;
  }
  unsigned char* const rows = static_cast<unsigned char*>(mapped) + footprint.Offset;
  const std::size_t row_bytes = static_cast<std::size_t>(pixels.width) * 4;
  const unsigned char* const source_rows = pixels.pixels->data();
  for (UINT y = 0; y < pixels.height; ++y) {
    std::memcpy(rows + static_cast<std::size_t>(y) * footprint.Footprint.RowPitch, source_rows + y * row_bytes,
                row_bytes);
  }
  const D3D12_RANGE written{0, static_cast<SIZE_T>(upload_bytes)};
  gpu.upload->Unmap(0, &written);

  D3D12_SHADER_RESOURCE_VIEW_DESC view{};
  view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  view.Texture2D.MipLevels = 1;
  D3D12_CPU_DESCRIPTOR_HANDLE cpu = g.srv_cpu_start;
  cpu.ptr += static_cast<SIZE_T>(1 + slot) * g.srv_increment;
  g.device->CreateShaderResourceView(gpu.resource, &view, cpu);

  D3D12_TEXTURE_COPY_LOCATION target{};
  target.pResource = gpu.resource;
  target.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  target.SubresourceIndex = 0;
  D3D12_TEXTURE_COPY_LOCATION source{};
  source.pResource = gpu.upload;
  source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  source.PlacedFootprint = footprint;
  g.upload_list->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
  transition(g.upload_list, gpu.resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  return true;
}

// Every texture waiting, recorded into the upload list (reset by the caller); answers how many.
std::uint32_t record_textures() {
  grace::abi::Registry& registry = session::registry();
  std::uint32_t recorded = 0;
  for (std::uint32_t slot = 0; slot < grace::abi::kMaxTextures; ++slot) {
    grace::abi::TexturePixels pixels;
    if (!registry.claim(slot, pixels)) {
      continue;
    }
    GpuTexture& gpu = g.textures[slot];
    gpu = GpuTexture{};
    const bool ok = record_texture(slot, pixels);
    registry.uploading(slot);  // its pixels are copied (or will never be): a mod's thread may free them
    if (!ok) {
      log_line("a mod's texture of %ux%u could not be made on the GPU: it is not drawn", pixels.width, pixels.height);
      registry.released(slot);
      continue;
    }
    ++recorded;
  }
  return recorded;
}

// Closes the upload list and executes it on the present queue; every texture it copied is drawable once the fence
// passes the value it signals.
bool submit_upload() {
  if (FAILED(g.upload_list->Close())) {
    return false;
  }
  ID3D12CommandList* const lists[] = {g.upload_list};
  g.queue->ExecuteCommandLists(1, lists);
  if (FAILED(g.queue->Signal(g.fence, ++g.fence_last))) {
    return false;
  }
  g.upload_fence = g.fence_last;
  grace::abi::Registry& registry = session::registry();
  for (std::uint32_t slot = 0; slot < grace::abi::kMaxTextures; ++slot) {
    if (registry.state(slot) == grace::abi::TextureState::uploading && g.textures[slot].copied_after == 0 &&
        g.textures[slot].resource != nullptr) {
      g.textures[slot].copied_after = g.upload_fence;
    }
  }
  state::texture_uploads.fetch_add(1, std::memory_order_relaxed);
  return true;
}

// Once a frame, on the present thread: textures whose copy has passed become drawable, those of mods that have gone are
// released once no frame can draw them, and textures registered since the last upload are uploaded in one batch, if
// the upload list is free. Never waits.
void update_textures() {
  grace::abi::Registry& registry = session::registry();
  bool waiting = false;
  for (std::uint32_t slot = 0; slot < grace::abi::kMaxTextures; ++slot) {
    GpuTexture& gpu = g.textures[slot];
    const grace::abi::TextureState now = registry.state(slot);
    if (now == grace::abi::TextureState::uploading && gpu.resource != nullptr && fence_passed(gpu.copied_after) &&
        gpu.copied_after != 0) {
      release(gpu.upload);
      registry.ready(slot);
      state::textures_uploaded.fetch_add(1, std::memory_order_relaxed);
    }
    if (registry.retiring(slot) && gpu.resource != nullptr &&
        (now == grace::abi::TextureState::uploading || now == grace::abi::TextureState::ready)) {
      if (!gpu.releasing) {
        gpu.releasing = true;  // no frame from now draws it (Registry::drawable): only those already submitted might
        gpu.release_after = std::max(g.fence_last, gpu.copied_after);
      } else if (fence_passed(gpu.release_after)) {
        release(gpu.upload);
        release(gpu.resource);
        gpu = GpuTexture{};
        registry.released(slot);
      }
    }
    waiting = waiting || (now == grace::abi::TextureState::pending && !registry.retiring(slot));
  }
  if (!waiting || !fence_passed(g.upload_fence)) {
    return;
  }
  if (FAILED(g.upload_allocator->Reset()) || FAILED(g.upload_list->Reset(g.upload_allocator, nullptr))) {
    turn_off("the upload list could not be reset");
    return;
  }
  const std::uint32_t recorded = record_textures();
  if (!submit_upload()) {
    turn_off("a texture's upload could not be submitted");
    return;
  }
  log_line("%u textures of a mod's uploaded after warm-up", recorded);
}

// The GPU descriptor a draw command's texture id names: the atlas, or a mod's texture that is ready; 0 for none (the
// command is not drawn, as for a texture still on its way).
ImTextureID texture_of(std::uint64_t id) noexcept {
  if (id == grace::abi::kAtlasTexture) {
    return static_cast<ImTextureID>(g.atlas_gpu.ptr);
  }
  const std::uint32_t slot = session::registry().drawable(id);
  if (slot >= grace::abi::kMaxTextures || g.textures[slot].resource == nullptr) {
    return ImTextureID{0};
  }
  return static_cast<ImTextureID>(texture_gpu(slot).ptr);
}

// ---- The shared atlas (D117, D139 as amended on 2026-11-16): baked on GraceUI's own thread, never on the present thread
// (a large face's bake took hundreds of milliseconds there, M3). At warm-up the present thread seals the mods' faces
// and asks; GraceUI's thread makes Dear ImGui's context, bakes every face it can (fewer, on a fresh atlas, if Dear ImGui
// runs out of room after all), makes the atlas's texture and its upload buffer on the game's device and fills it, and
// frees the CPU copy; the present thread then records the one copy on the present queue and polls its fence.

enum class Bake : std::uint32_t { none = 0, asked = 1, baking = 2, done = 3, failed = 4 };
constinit std::atomic<Bake> g_bake{Bake::none};

// What GraceUI's thread makes for the present thread: written before `done` is stored, read after it is loaded.
struct BakedAtlas {
  ID3D12Device* device;  // borrowed: the present thread's reference, never released while a bake runs (try_teardown)
  ID3D12Resource* texture;
  ID3D12Resource* upload;  // the atlas's pixels, row by row as the copy reads them
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
  UINT width;
  UINT height;
};
constinit BakedAtlas g_baked{};

// A context of Dear ImGui's, configured as GraceUI's (D122, D134), current from now on.
void make_context() {
  ImGuiContext* context = ImGui::CreateContext();
  ImGui::SetCurrentContext(context);
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;  // Dear ImGui writes no file (D84: GraceUI writes only under its user folder)
  io.LogFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;  // the cursor is the input half's (§5.5)
  io.ConfigErrorRecoveryEnableTooltip = false;              // nothing of Dear ImGui's own is ever drawn on the game
  // Dear ImGui frees a draw list unused for this long of its own time, and makes it again at its next use: on the
  // present thread, during play, when a notice comes after a quiet spell. Never, then (D134; invariant 10).
  io.ConfigMemoryCompactTimer = FLT_MAX;
  input::reserve_imgui_queues();  // a holder's input then allocates nothing during play (invariant 10)
}

// A context whose bake failed is left as it is: Dear ImGui's state after a failed assertion is not trusted to be torn
// down. Only its atlas's pixels, the bulk of it, are freed.
void abandon_context() noexcept {
  for (ImTextureData* texture : ImGui::GetIO().Fonts->TexList) {
    texture->DestroyPixels();
  }
}

// The atlas's texture and its filled upload buffer, on the game's device (whose object creation any thread may call).
bool make_gpu_atlas(const ImTextureData& texture) {
  const UINT width = static_cast<UINT>(texture.Width);
  const UINT height = static_cast<UINT>(texture.Height);
  ID3D12Device* const device = g_baked.device;
  D3D12_HEAP_PROPERTIES device_heap{};
  device_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  if (FAILED(device->CreateCommittedResource(&device_heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                             nullptr, IID_PPV_ARGS(&g_baked.texture)))) {
    return false;
  }
  UINT64 upload_bytes = 0;
  device->GetCopyableFootprints(&desc, 0, 1, 0, &g_baked.footprint, nullptr, nullptr, &upload_bytes);
  D3D12_HEAP_PROPERTIES upload_heap{};
  upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC buffer_desc{};
  buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer_desc.Width = upload_bytes;
  buffer_desc.Height = 1;
  buffer_desc.DepthOrArraySize = 1;
  buffer_desc.MipLevels = 1;
  buffer_desc.Format = DXGI_FORMAT_UNKNOWN;
  buffer_desc.SampleDesc.Count = 1;
  buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (FAILED(device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                             IID_PPV_ARGS(&g_baked.upload)))) {
    return false;
  }
  void* mapped = nullptr;
  const D3D12_RANGE nothing_read{0, 0};
  if (FAILED(g_baked.upload->Map(0, &nothing_read, &mapped))) {
    return false;
  }
  unsigned char* const rows = static_cast<unsigned char*>(mapped) + g_baked.footprint.Offset;
  const unsigned char* const pixels = texture.Pixels;
  const std::size_t row_bytes = static_cast<std::size_t>(width) * 4;
  for (UINT y = 0; y < height; ++y) {
    std::memcpy(rows + static_cast<std::size_t>(y) * g_baked.footprint.Footprint.RowPitch, pixels + y * row_bytes,
                row_bytes);
  }
  const D3D12_RANGE written{0, static_cast<SIZE_T>(upload_bytes)};
  g_baked.upload->Unmap(0, &written);
  g_baked.width = width;
  g_baked.height = height;
  return true;
}

// GraceUI's thread: the context, the bake (every face, then half the mods', then none of theirs, should Dear ImGui run
// out of room although each face was counted against the atlas's room), and the atlas's GPU objects.
bool bake_atlas() {
#if defined(GRACEUI_TEST_EXPORTS)
  ImGui::SetAllocatorFunctions(&draw_alloc::imgui_alloc, &draw_alloc::imgui_free, nullptr);
#endif
  IMGUI_CHECKVERSION();
  make_context();
  LARGE_INTEGER started{};
  QueryPerformanceCounter(&started);
  std::uint32_t mod_faces = session::registry().faces();
  float scale = menu::bake_scale(g.height);  // the menu's faces at the effective scale (§3.13, D167)
  const float content = display::content_scale(g.height);  // the mods' scaled faces' (D180)
  for (;;) {
    try {
      atlas::bake(ImGui::GetIO().Fonts, mod_faces, scale, content);
      break;
    } catch (const ImGuiAssertion& failed) {
      log_line("the shared atlas could not be baked with %u of the mods' faces%s (a Dear ImGui assertion at line %d: "
               "%.60s)",
               mod_faces, scale > 0.0f ? " and the menu's" : "", failed.line, failed.expression);
      if (mod_faces == 0 && scale == 0.0f) {
        return false;
      }
      abandon_context();
      make_context();
      if (mod_faces == 0) {
        scale = 0.0f;  // the last try leaves the menu's faces out: notices before the menu (D167)
      }
      mod_faces /= 2;
    }
  }
  ImFontAtlas* fonts = ImGui::GetIO().Fonts;
  const ImTextureData* texture = fonts->TexData;
  if (texture == nullptr || texture->Pixels == nullptr || texture->Format != ImTextureFormat_RGBA32 ||
      !make_gpu_atlas(*texture)) {
    log_line("the shared atlas could not be made on the game's device");
    return false;
  }
  // The pixels are in the upload buffer: the CPU copy is freed here rather than kept for the session (Dear ImGui's
  // own call for an atlas a renderer without its texture path has uploaded).
  fonts->ClearTexData();
  LARGE_INTEGER done{};
  LARGE_INTEGER frequency{};
  QueryPerformanceCounter(&done);
  QueryPerformanceFrequency(&frequency);
  log_line("the shared atlas was baked in %lld ms on GraceUI's own thread",
           frequency.QuadPart > 0 ? static_cast<long long>((done.QuadPart - started.QuadPart) * 1000 / frequency.QuadPart)
                                  : 0LL);
  display::set_faces_scale(content);  // the mods hear of the display from now (D180)
  return true;
}

// The atlas (D117): GraceUI uploads it itself, once, on the present queue, and polls its fence on later frames: the
// DX12 backend's own texture path would wait for the GPU on this thread. Its texture and its filled upload buffer come
// from GraceUI's thread (bake_atlas); this records the copy.
bool start_atlas() {
  g.atlas = g_baked.texture;
  g.atlas_upload = g_baked.upload;
  g_baked.texture = nullptr;
  g_baked.upload = nullptr;
  const UINT width = g_baked.width;
  const UINT height = g_baked.height;

  D3D12_SHADER_RESOURCE_VIEW_DESC view{};
  view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  view.Texture2D.MipLevels = 1;
  g.device->CreateShaderResourceView(g.atlas, &view, g.srv_cpu_start);
  g.atlas_gpu = g.srv_gpu_start;
  g.srv_used = 1 + kTextureDescriptors;

  // The copy runs on the present queue, so every later frame of ours, on the same queue, comes after it.
  if (FAILED(g.upload_allocator->Reset()) || FAILED(g.upload_list->Reset(g.upload_allocator, nullptr))) {
    return false;
  }
  D3D12_TEXTURE_COPY_LOCATION target{};
  target.pResource = g.atlas;
  target.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  target.SubresourceIndex = 0;
  D3D12_TEXTURE_COPY_LOCATION source{};
  source.pResource = g.atlas_upload;
  source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  source.PlacedFootprint = g_baked.footprint;
  g.upload_list->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
  transition(g.upload_list, g.atlas, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  // Every mod's texture registered before warm-up joins the same copy (D76).
  const std::uint32_t textures = record_textures();
  if (!submit_upload()) {
    return false;
  }
  state::reach_frame_step(8);  // the atlas's upload submitted, with the mods' textures (D230)
  g.atlas_fence = g.fence_last;
  if (textures != 0) {
    log_line("%u textures of mods' joined the atlas's upload", textures);
  }
  state::atlas_width.store(width, std::memory_order_relaxed);
  state::atlas_height.store(height, std::memory_order_relaxed);
  log_line("atlas baked at %ux%u; its one upload is queued", width, height);
  return true;
}

// Answers whether the atlas is on the GPU, finishing it the first time its upload's fence has passed.
bool finish_atlas() noexcept {
  if (g.atlas_ready) {
    return true;
  }
  if (!fence_passed(g.atlas_fence)) {
    return false;  // still copying: draw nothing this frame rather than wait
  }
  ImTextureData* texture = ImGui::GetIO().Fonts->TexData;
  texture->SetTexID(static_cast<ImTextureID>(g.atlas_gpu.ptr));
  texture->SetStatus(ImTextureStatus_OK);
  release(g.atlas_upload);
  g.atlas_ready = true;
  state::atlas_ready.store(true, std::memory_order_relaxed);
  state::reach_frame_step(9);  // D230
  return true;
}

bool build_renderer() {
  D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{};
  rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_desc.NumDescriptors = kMaxBuffers;
  if (FAILED(g.device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&g.rtv_heap)))) {
    return false;
  }
  D3D12_DESCRIPTOR_HEAP_DESC srv_desc{};
  srv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  srv_desc.NumDescriptors = kSrvDescriptors;
  srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(g.device->CreateDescriptorHeap(&srv_desc, IID_PPV_ARGS(&g.srv_heap)))) {
    return false;
  }
  g.rtv_start = g.rtv_heap->GetCPUDescriptorHandleForHeapStart();
  g.srv_cpu_start = g.srv_heap->GetCPUDescriptorHandleForHeapStart();
  g.srv_gpu_start = g.srv_heap->GetGPUDescriptorHandleForHeapStart();
  g.rtv_increment = g.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  g.srv_increment = g.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  if (FAILED(g.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g.fence)))) {
    return false;
  }
  for (Slot& slot : g.slots) {
    if (FAILED(g.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&slot.allocator))) ||
        FAILED(g.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot.allocator, nullptr,
                                           IID_PPV_ARGS(&slot.list))) ||
        FAILED(slot.list->Close())) {
      return false;
    }
    slot.fence_value = 0;
  }
  if (FAILED(g.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g.upload_allocator))) ||
      FAILED(g.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g.upload_allocator, nullptr,
                                         IID_PPV_ARGS(&g.upload_list))) ||
      FAILED(g.upload_list->Close())) {
    return false;
  }

  // Dear ImGui's context and the baked atlas are GraceUI's thread's (bake_atlas), made before this runs.
  state::reach_frame_step(6);  // the renderer's own objects made on the game's device (D230)
  if (!ImGui_ImplWin32_Init(g.window) || !init_dx12_backend()) {
    return false;
  }
  state::reach_frame_step(7);  // D230
  if (!start_atlas()) {
    return false;
  }
  LARGE_INTEGER frequency{};
  QueryPerformanceFrequency(&frequency);
  g.qpc_frequency = frequency.QuadPart;
  g.qpc_last = qpc_now();
  g.ready = true;
  state::renderer_ready.store(true, std::memory_order_relaxed);
  return true;
}

// The swap chain's format changed (ResizeBuffers, or a new swap chain): the DX12 backend's pipeline states are built
// for one format, so the backend is shut down and initialised again once none of our frames is on the GPU.
bool rebuild_for_format() {
  if (!fence_passed(g.fence_last)) {
    return false;
  }
  ImGui_ImplDX12_Shutdown();
  g.dx12_live = false;
  if (!init_dx12_backend()) {
    turn_off("the DX12 backend could not be rebuilt for the swap chain's new format");
    return false;
  }
  // Shutdown marked every texture it saw as destroyed; the atlas was never the backend's: it is ours, still in place.
  ImTextureData* texture = ImGui::GetIO().Fonts->TexData;
  texture->SetTexID(static_cast<ImTextureID>(g.atlas_gpu.ptr));
  texture->SetStatus(ImTextureStatus_OK);
  for (Slot& slot : g.slots) {
    slot.fence_value = 0;
  }
  g.format_pending = false;
  log_line("the swap chain's format changed; the DX12 backend was rebuilt for it");
  return true;
}

BackBuffer* back_buffer(UINT index) noexcept {
  if (index >= kMaxBuffers || index >= g.buffer_count) {
    return nullptr;
  }
  BackBuffer& buffer = g.buffers[index];
  if (buffer.resource == nullptr) {
    ID3D12Resource* resource = nullptr;
    if (FAILED(g.swap_chain->GetBuffer(index, IID_PPV_ARGS(&resource)))) {
      return nullptr;
    }
    buffer.rtv.ptr = g.rtv_start.ptr + static_cast<SIZE_T>(index) * g.rtv_increment;
    g.device->CreateRenderTargetView(resource, nullptr, buffer.rtv);
    buffer.resource = resource;
    // D118: no reference is kept between frames. The swap chain keeps the buffer alive until ResizeBuffers or its own
    // end, and hooks.cpp calls mark_buffers_stale() before the one and unbind() after the other.
    resource->Release();
  }
  return &buffer;
}

// After ResizeBuffers: forget the old buffers and read the new size, count and format (D118).
void reread_swap_chain() noexcept {
  forget_back_buffers();
  DXGI_SWAP_CHAIN_DESC1 desc{};
  if (FAILED(g.swap_chain->GetDesc1(&desc))) {
    return;
  }
  g.width = desc.Width;
  g.height = desc.Height;
  display::set_size(g.width, g.height);  // a resize: the mods hear of it (D180)
  g.buffer_count = desc.BufferCount;
  if (desc.Format != g.format) {
    g.format = desc.Format;
    g.format_pending = g.ready;
  }
}

bool notice_showing(const grace::abi::NoticeList& notices, std::int64_t now_ms) noexcept {
  for (std::uint32_t i = 0; i < notices.count; ++i) {
    if (notices.items[i].live_at(now_ms)) {
      return true;
    }
  }
  return false;
}

// The test panel stands for GraceUI's own drawing: the notice-only mode draws nothing but notices (D88).
bool test_panel_on() noexcept {
  return state::test_panel.load(std::memory_order_relaxed) && !session::notice_only();
}

// Whether any panel is shown with a list to draw (its newest published one is checked when it is drawn).
bool panels_showing() noexcept {
  if (session::notice_only()) {
    return false;
  }
  grace::abi::Panels& panels = session::panels();
  for (std::uint32_t i = 0; i < grace::abi::kMaxPanels; ++i) {
    grace::abi::PanelView view;
    if (panels.view(i, view) && view.visible && panels.lists(i).published() != 0) {
      return true;
    }
  }
  return false;
}

bool wants_to_draw(const grace::abi::NoticeList& notices, std::int64_t now_ms) noexcept {
  return test_panel_on() || input::wants_frames() || notice_showing(notices, now_ms) || panels_showing();
}

// ---- Notices (D134, §3.8).

struct NoticeLine {
  const char* begin;
  const char* end;
};

// A notice's text in lines that fit `wrap` pixels, at most kNoticeLines; answers how many. Words wrap as Dear ImGui
// wraps them; a line feed ends a line.
int lay_out(ImFont* font, float size, float wrap, const char* text, const char* end, NoticeLine (&lines)[kNoticeLines]) {
  int count = 0;
  const char* at = text;
  while (at < end && count < kNoticeLines) {
    const char* feed = static_cast<const char*>(std::memchr(at, '\n', static_cast<std::size_t>(end - at)));
    const char* stop = feed != nullptr ? feed : end;
    const char* cut = font->CalcWordWrapPosition(size, at, stop, wrap);
    if (cut <= at) {
      cut = at + 1;  // not even one character fits: take one byte, then its continuation bytes
      while (cut < stop && (static_cast<unsigned char>(*cut) & 0xC0u) == 0x80u) {
        ++cut;
      }
    }
    lines[count++] = NoticeLine{at, cut};
    at = cut;
    if (at < end && *at == '\n') {
      ++at;
    }
    while (at < stop && *at == ' ') {
      ++at;
    }
  }
  return count;
}

// Ember's icon is a triangle, rot's a circle, each with a mark of exclamation in the ground's colour (D25).
void draw_icon(ImDrawList* list, std::uint32_t tone, float x, float centre_y) {
  const ImU32 colour = kAccents[tone];
  const float half = kNoticeIcon * 0.5f;
  if (tone == GRACEUI_TONE_WARNING) {
    list->AddTriangleFilled(ImVec2(x + half, centre_y - half), ImVec2(x + kNoticeIcon, centre_y + half),
                            ImVec2(x, centre_y + half), colour);
  } else {
    list->AddCircleFilled(ImVec2(x + half, centre_y), half, colour, 16);
  }
  const ImU32 mark = opaque(kGround2);
  list->AddRectFilled(ImVec2(x + half - 1.0f, centre_y - half + 3.5f), ImVec2(x + half + 1.0f, centre_y + 1.5f), mark);
  list->AddRectFilled(ImVec2(x + half - 1.0f, centre_y + 2.5f), ImVec2(x + half + 1.0f, centre_y + 4.5f), mark);
}

// The Grace pointer (G31, D178), drawn in Dear ImGui's place for a holder that takes the mouse: at `at`, the hotspot, at
// the effective scale `s` and in the theme's colours. A text beam where Dear ImGui asks for one. Into the foreground
// list, within the room reserved at warm-up; warm-up's frame draws both once, so that their first use in play allocates
// nothing (invariant 10).
constexpr ImU32 tinted(std::uint32_t rgb, float alpha) noexcept {
  return (static_cast<ImU32>(alpha * 255.0f + 0.5f) << IM_COL32_A_SHIFT) | (opaque(rgb) & ~IM_COL32_A_MASK);
}

void draw_pointer(ImDrawList* list, ImVec2 at, ImGuiMouseCursor shape, float s, bool clear) {
  namespace pointer = grace::draw;
  const grace::tokens::Palette& colours = clear ? grace::tokens::kClear.colour : grace::tokens::kGrace.colour;
  const ImU32 edge = opaque(colours.gold_hi.rgb());
  const ImU32 fill = tinted(colours.ground_0.rgb(), pointer::kPointerFillAlpha);
  const ImU32 halo = tinted(colours.ground_0.rgb(), pointer::kPointerHaloAlpha);
  const float edge_width = (clear ? pointer::kPointerEdgeClear : pointer::kPointerEdge) * s;
  if (shape == ImGuiMouseCursor_TextInput) {
    const float half = pointer::kBeamHeight * s * 0.5f;
    const float stem = pointer::kBeamStem * s * 0.5f;
    const float bar = pointer::kBeamBar * s * 0.5f;
    const float pad = (pointer::kPointerHalo - pointer::kPointerEdge) * 0.5f * s;
    const ImVec4 parts[3] = {{at.x - stem, at.y - half, at.x + stem, at.y + half},
                             {at.x - bar, at.y - half, at.x + bar, at.y - half + edge_width},
                             {at.x - bar, at.y + half - edge_width, at.x + bar, at.y + half}};
    for (const ImVec4& r : parts) {
      list->AddRectFilled(ImVec2(r.x - pad, r.y - pad), ImVec2(r.z + pad, r.w + pad), halo, pad + stem);
    }
    for (const ImVec4& r : parts) {
      list->AddRectFilled(ImVec2(r.x, r.y), ImVec2(r.z, r.w), edge, stem);
    }
    return;
  }
  ImVec2 points[pointer::kPointerPoints];
  for (std::size_t i = 0; i < pointer::kPointerPoints; ++i) {
    points[i] = ImVec2(at.x + pointer::kPointerOutline[i].x * s, at.y + pointer::kPointerOutline[i].y * s);
  }
  constexpr int count = static_cast<int>(pointer::kPointerPoints);
  list->AddPolyline(points, count, halo, pointer::kPointerHalo * s, ImDrawFlags_Closed);
  list->AddConcavePolyFilled(points, count, fill);
  list->AddPolyline(points, count, edge, edge_width, ImDrawFlags_Closed);
}

void draw_notices(const grace::abi::NoticeList& notices, std::int64_t now_ms) {
  const float display_width = static_cast<float>(g.width);
  const float width = std::min(kNoticeWidth, display_width - 2.0f * kNoticeMargin);
  ImFont* font = atlas::notice_font();
  if (width < 96.0f || font == nullptr) {
    return;
  }
  ImDrawList* list = ImGui::GetForegroundDrawList();
  const float size = font->LegacySize;
  const float line_height = atlas::notice_face_is_grace() ? atlas::kNoticeLineHeight : size + 4.0f;
  const float x1 = display_width - kNoticeMargin;
  const float x0 = x1 - width;
  float y = kNoticeMargin;
  std::uint32_t drawn = 0;
  for (std::uint32_t i = 0; i < notices.count && drawn < kNoticesDrawn && y < static_cast<float>(g.height); ++i) {
    const grace::abi::Notice& notice = notices.items[i];
    if (!notice.live_at(now_ms)) {
      continue;
    }
    const std::uint32_t tone = notice.tone <= GRACEUI_TONE_DANGER ? notice.tone : GRACEUI_TONE_NORMAL;
    const float icon = tone == GRACEUI_TONE_NORMAL ? 0.0f : kNoticeIcon + kNoticeIconGap;
    const float text_x = x0 + kNoticeAccent + kNoticePadX + icon;
    const float wrap = x1 - kNoticePadX - text_x;
    NoticeLine lines[kNoticeLines];
    const int count = lay_out(font, size, wrap, notice.text, notice.text + notice.text_length, lines);
    const float y1 = y + 2.0f * kNoticePadY + static_cast<float>(std::max(count, 1)) * line_height;
    list->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y1), opaque(kGround2), kNoticeRadius);
    list->AddRect(ImVec2(x0, y), ImVec2(x1, y1), opaque(kRule), kNoticeRadius, 1.0f);
    list->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + kNoticeAccent, y1), kAccents[tone], kNoticeRadius,
                        ImDrawFlags_RoundLeft);  // over the border: the accent is the card's left edge
    const float inset = (line_height - size) * 0.5f;
    if (icon > 0.0f) {
      draw_icon(list, tone, x0 + kNoticeAccent + kNoticePadX, y + kNoticePadY + line_height * 0.5f);
    }
    for (int line = 0; line < count; ++line) {
      list->AddText(font, size, ImVec2(text_x, y + kNoticePadY + inset + static_cast<float>(line) * line_height),
                    opaque(kInkHigh), lines[line].begin, lines[line].end);
    }
    y = y1 + kNoticeGap;
    ++drawn;
  }
}

void draw_test_panel() {
#if defined(GRACEUI_TEST_EXPORTS)
  if (!test_panel_on()) {
    return;
  }
  constexpr ImU32 panel = (255u << IM_COL32_A_SHIFT) | (unsigned{GRACEUI_TEST_PANEL_B} << IM_COL32_B_SHIFT) |
                          (unsigned{GRACEUI_TEST_PANEL_G} << IM_COL32_G_SHIFT) |
                          (unsigned{GRACEUI_TEST_PANEL_R} << IM_COL32_R_SHIFT);
  constexpr ImU32 white = 0xFFFFFFFFu;
  // GraceUI's own drawing, on the menu's layer: above the mods' hud and panel layers, below their modals (D76).
  ImDrawList* list = ImGui::GetBackgroundDrawList();
  list->AddRectFilled(ImVec2(float{GRACEUI_TEST_PANEL_X0}, float{GRACEUI_TEST_PANEL_Y0}),
                      ImVec2(float{GRACEUI_TEST_PANEL_X1}, float{GRACEUI_TEST_PANEL_Y1}), panel);
  list->AddText(ImVec2(float{GRACEUI_TEST_TEXT_X}, float{GRACEUI_TEST_TEXT_Y}), white, GRACEUI_TEST_TEXT);
#endif
}

// ---- Panels (D76): each shown panel's newest list, moved to where its anchor puts it and clipped to it.

// The lists of one frame, by layer: hud, panel, modal.
struct PanelLayers {
  ImDrawList* lists[3][grace::abi::kMaxPanels];
  int counts[3];
};

int layer_index(std::uint32_t layer) noexcept {
  return layer == GRACEUI_LAYER_HUD ? 0 : layer == GRACEUI_LAYER_PANEL ? 1 : 2;
}

// Lends `data`'s arrays to `list` for this frame, its commands' clips moved by (x0, y0) and kept within `bounds`
// (x0, y0, x1, y1): false if nothing of it is to be drawn.
bool lend(ImDrawList* list, grace::abi::DrawListData& data, float x0, float y0, const ImVec4& bounds) noexcept {
  list->CmdBuffer.resize(0);  // within the room reserved at warm-up: no allocation
  for (const GraceUiDrawCommand& command : data.commands) {
    const ImTextureID texture = texture_of(command.texture);
    if (command.index_count == 0 || texture == ImTextureID{0}) {
      continue;  // nothing to draw, or a texture not yet on the GPU
    }
    ImDrawCmd cmd;
    cmd.ClipRect = ImVec4(std::max(command.clip_x0 + x0, bounds.x), std::max(command.clip_y0 + y0, bounds.y),
                          std::min(command.clip_x1 + x0, bounds.z), std::min(command.clip_y1 + y0, bounds.w));
    if (cmd.ClipRect.z <= cmd.ClipRect.x || cmd.ClipRect.w <= cmd.ClipRect.y) {
      continue;
    }
    cmd.TexRef = ImTextureRef(texture);
    cmd.VtxOffset = command.vertex_offset;  // the backend honours it (ImGuiBackendFlags_RendererHasVtxOffset)
    cmd.IdxOffset = command.index_offset;
    cmd.ElemCount = command.index_count;
    list->CmdBuffer.push_back(cmd);
  }
  if (list->CmdBuffer.Size == 0) {
    return false;
  }
  // SAFETY: rule (4) above: the list's arrays, lent for this frame's RenderDrawData only.
  list->VtxBuffer.Data = reinterpret_cast<ImDrawVert*>(data.vertices.data());
  list->VtxBuffer.Size = list->VtxBuffer.Capacity = static_cast<int>(data.vertices.size());
  list->IdxBuffer.Data = data.indices.data();
  list->IdxBuffer.Size = list->IdxBuffer.Capacity = static_cast<int>(data.indices.size());
  return true;
}

void gather_panels(PanelLayers& layers) noexcept {
  layers.counts[0] = layers.counts[1] = layers.counts[2] = 0;
  if (session::notice_only() || g_panel_lists[0] == nullptr) {
    return;
  }
  const float display_width = static_cast<float>(g.width);
  const float display_height = static_cast<float>(g.height);
  grace::abi::Panels& panels = session::panels();
  for (std::uint32_t i = 0; i < grace::abi::kMaxPanels; ++i) {
    grace::abi::PanelView view;
    if (!panels.view(i, view) || !view.visible) {
      continue;
    }
    grace::abi::DrawListData& data = panels.lists(i).read();
    if (data.serial == 0 || data.panel_generation != static_cast<std::uint32_t>(view.id >> 8) ||
        data.commands.empty()) {
      continue;  // nothing published yet, or a list of the slot's earlier panel
    }
    const grace::abi::Rect rect = grace::abi::place(view.desc, display_width, display_height);
    if (data.placed_x != rect.x0 || data.placed_y != rect.y0) {
      // Moved where the panel lies, once: the list stays this thread's until a newer one comes.
      const float dx = rect.x0 - data.placed_x;
      const float dy = rect.y0 - data.placed_y;
      for (GraceUiDrawVert& vertex : data.vertices) {
        vertex.x += dx;
        vertex.y += dy;
      }
      data.placed_x = rect.x0;
      data.placed_y = rect.y0;
    }
    ImDrawList* list = g_panel_lists[i];
    if (!lend(list, data, rect.x0, rect.y0,
              ImVec4(std::max(rect.x0, 0.0f), std::max(rect.y0, 0.0f), std::min(rect.x1, display_width),
                     std::min(rect.y1, display_height)))) {
      continue;
    }
    const int layer = layer_index(view.desc.layer);
    layers.lists[layer][layers.counts[layer]++] = list;
  }
}

// Takes back every array lent to a list (SAFETY (4)), so that no ImDrawList of GraceUI's points into a buffer.
void take_back(ImDrawList* list) noexcept {
  if (list != nullptr) {
    list->VtxBuffer.Data = nullptr;
    list->VtxBuffer.Size = list->VtxBuffer.Capacity = 0;
    list->IdxBuffer.Data = nullptr;
    list->IdxBuffer.Size = list->IdxBuffer.Capacity = 0;
  }
}

void take_back_panels() noexcept {
  for (ImDrawList* list : g_panel_lists) {
    take_back(list);
  }
  take_back(g_menu_list);
}

// Warm-up's last step (D134): one frame of Dear ImGui's, built and never submitted, which makes the foreground and
// background draw lists that the notices and the test panel use and reserves their room, so that no notice allocates
// on the present thread during play (invariant 10); and the panels' lists and the frame's draw data, with their room.
void prime_frame() {
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(static_cast<float>(std::max(g.width, 1u)), static_cast<float>(std::max(g.height, 1u)));
  io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
  io.DeltaTime = 1.0f / 60.0f;
  ImGui_ImplDX12_NewFrame();
  ImGui::NewFrame();
  for (ImDrawList* list : {ImGui::GetForegroundDrawList(), ImGui::GetBackgroundDrawList()}) {
    list->VtxBuffer.reserve(kReservedVertices);
    list->IdxBuffer.reserve(kReservedIndices);
    list->CmdBuffer.reserve(16);
  }
  // The pointer's shapes once, off the display: their scratch room grows here, not at their first use in play.
  for (const ImGuiMouseCursor shape : {ImGuiMouseCursor_Arrow, ImGuiMouseCursor_TextInput}) {
    draw_pointer(ImGui::GetForegroundDrawList(), ImVec2(-1000.0f, -1000.0f), shape, 4.0f, true);
  }
  ImGui::Render();
  for (ImDrawList*& list : g_panel_lists) {
    list = IM_NEW(ImDrawList)(ImGui::GetDrawListSharedData());
    list->CmdBuffer.reserve(static_cast<int>(grace::abi::kDrawCommandsMax));
  }
  g_menu_list = IM_NEW(ImDrawList)(ImGui::GetDrawListSharedData());
  g_menu_list->CmdBuffer.reserve(static_cast<int>(grace::abi::kDrawCommandsMax));
  g_frame = IM_NEW(ImDrawData)();
  g_frame->CmdLists.reserve(kFrameLists);
  g.qpc_last = qpc_now();
  g.primed = true;
}

void add_list(ImDrawList* list) {
  if (g_frame->CmdLists.Size < kFrameLists) {  // within the room reserved at warm-up
    g_frame->CmdLists.push_back(list);
    g_frame->TotalVtxCount += list->VtxBuffer.Size;
    g_frame->TotalIdxCount += list->IdxBuffer.Size;
  }
}

ImDrawData* build_frame(const grace::abi::NoticeList& notices, std::int64_t now_ms) {
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(static_cast<float>(g.width), static_cast<float>(g.height));
  io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
  const std::int64_t now = qpc_now();
  const double seconds = static_cast<double>(now - g.qpc_last) / static_cast<double>(g.qpc_frequency);
  io.DeltaTime = seconds > 1e-6 ? static_cast<float>(seconds) : 1e-6f;
  g.qpc_last = now;
  ImGui_ImplDX12_NewFrame();
  input::before_imgui_frame(g.width, g.height);  // the holder's input, if a holder has it (D77, D125)
  ImGui::NewFrame();
  input::after_new_frame();
  draw_test_panel();
  draw_notices(notices, now_ms);  // the foreground list: above every layer but the holder's pointer (D76)
  ImDrawList* const foreground = ImGui::GetForegroundDrawList();
  if (io.MouseDrawCursor) {
    // The Grace pointer in Dear ImGui's place (G31, D178): the shape it asked for, and none of its own drawn.
    const ImGuiMouseCursor shape = ImGui::GetMouseCursor();
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    if (shape != ImGuiMouseCursor_None && ImGui::IsMousePosValid()) {
      draw_pointer(foreground, io.MousePos, shape, menu::bake_scale(g.height), menu::clear_theme());
    }
  }
  ImGui::Render();
  const ImDrawData* own = ImGui::GetDrawData();

  // The layers (D76): the mods' hud, then panel, then GraceUI's own drawing (the menu's layer), then the mods' modal,
  // then the notices (and Dear ImGui's cursor for a holder). Within a layer, panels in the order of their slots.
  PanelLayers layers;
  gather_panels(layers);
  g_frame->Clear();
  g_frame->Valid = true;
  g_frame->DisplayPos = ImVec2(0.0f, 0.0f);
  g_frame->DisplaySize = io.DisplaySize;
  g_frame->FramebufferScale = ImVec2(1.0f, 1.0f);
  g_frame->OwnerViewport = own->OwnerViewport;
  for (int layer = 0; layer < 2; ++layer) {
    for (int i = 0; i < layers.counts[layer]; ++i) {
      add_list(layers.lists[layer][i]);
    }
  }
  bool foreground_drawn = false;
  for (ImDrawList* list : own->CmdLists) {
    if (list == foreground) {
      foreground_drawn = true;
    } else {
      add_list(list);
    }
  }
  // GraceUI's menu, in GraceUI's own layer (D167): its newest list for the holder now, when the menu holds input.
  if (grace::abi::DrawListData* menu_list = menu::frame(g.width, g.height);
      menu_list != nullptr && g_menu_list != nullptr &&
      lend(g_menu_list, *menu_list, 0.0f, 0.0f, ImVec4(0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y))) {
    add_list(g_menu_list);
  }
  for (int i = 0; i < layers.counts[2]; ++i) {
    add_list(layers.lists[2][i]);
  }
  if (foreground_drawn) {
    add_list(foreground);
  }
  if (layers.counts[0] + layers.counts[1] + layers.counts[2] > 0) {
    state::panel_frames.fetch_add(1, std::memory_order_relaxed);
  }
  return g_frame;
}

void record_and_submit(Slot& slot, const BackBuffer& buffer, ImDrawData* data) {
  if (FAILED(slot.allocator->Reset()) || FAILED(slot.list->Reset(slot.allocator, nullptr))) {
    turn_off("a command allocator could not be reset");
    return;
  }
  transition(slot.list, buffer.resource, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
  slot.list->OMSetRenderTargets(1, &buffer.rtv, FALSE, nullptr);
  ID3D12DescriptorHeap* const heaps[] = {g.srv_heap};
  slot.list->SetDescriptorHeaps(1, heaps);
  ImGui_ImplDX12_RenderDrawData(data, slot.list);
  ++g.draws;  // the backend advanced its frame index: keep step with it whatever follows (D119)
  transition(slot.list, buffer.resource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
  if (FAILED(slot.list->Close())) {
    turn_off("a frame's command list could not be closed");
    return;
  }
  ID3D12CommandList* const lists[] = {slot.list};
#if defined(GRACEUI_TEST_EXPORTS)
  const std::int64_t submit_start = qpc_now();
#endif
  g.queue->ExecuteCommandLists(1, lists);
  const HRESULT signalled = g.queue->Signal(g.fence, ++g.fence_last);
#if defined(GRACEUI_TEST_EXPORTS)
  counters::frame_submit_ticks.fetch_add(qpc_now() - submit_start, std::memory_order_relaxed);
#endif
  if (FAILED(signalled)) {
    turn_off("the present queue refused GraceUI's fence");
    return;
  }
  slot.fence_value = g.fence_last;
  state::frames_drawn.fetch_add(1, std::memory_order_relaxed);
  state::reach_frame_step(10);  // D230
}

// Warm-up (D117, D139 as amended): the faces sealed and the bake asked of GraceUI's thread; nothing drawn until it is
// done; then the renderer built on this thread, which records the atlas's one upload. Answers whether it is ready.
bool warm_up() {
  switch (g_bake.load(std::memory_order_acquire)) {
    case Bake::none:
      session::registry().seal_faces();  // from now on register_face answers too late (D80)
      g_baked.device = g.device;
      g_bake.store(Bake::asked, std::memory_order_release);
      state::reach_frame_step(4);  // D230
      hooks::wake();
      return false;
    case Bake::asked:
    case Bake::baking: return false;
    case Bake::failed:
      turn_off("the shared atlas could not be baked");
      return false;
    case Bake::done: break;
  }
  state::reach_frame_step(5);  // D230
  if (!build_renderer()) {
    turn_off("the renderer could not be built on the game's device");
    return false;
  }
  return true;
}

// ---- Letting go of the game's device (L3). Direct3D 12 hands out one device per adapter while any reference to it
// lives, so a reference of GraceUI's would hand a game that recreates its device after a removal the removed one again,
// and would keep a replaced device's memory for the session. Once the device is removed or changed, GraceUI releases
// everything it made on it and the device itself, as soon as no frame of its own can still be on the GPU (at once for
// a removed device), never waiting; the render path stays off for the session (a later version rebuilds on the new one).

void release_device_objects() {
  if (g.dx12_live) {
    g.dx12_live = false;
    ImGui_ImplDX12_Shutdown();  // releases the backend's own objects; it waits for nothing
  }
  for (Slot& slot : g.slots) {
    release(slot.list);
    release(slot.allocator);
  }
  release(g.upload_list);
  release(g.upload_allocator);
  release(g.atlas_upload);
  release(g.atlas);
  release(g_baked.upload);
  release(g_baked.texture);
  for (GpuTexture& texture : g.textures) {
    release(texture.upload);
    release(texture.resource);
  }
  release(g.rtv_heap);
  release(g.srv_heap);
  release(g.fence);
  release(g.retired_queue);
  release(g.queue);
  forget_back_buffers();
  g.swap_chain = nullptr;
  release(g.device);
  g_baked.device = nullptr;
  state::queue_method.store(QueueMethod::none, std::memory_order_relaxed);
}

void try_teardown() noexcept {
  if (g.device == nullptr) {
    g.teardown = false;
    return;
  }
  Bake asked = Bake::asked;
  g_bake.compare_exchange_strong(asked, Bake::failed, std::memory_order_acq_rel);  // not begun: it never will be
  if (g_bake.load(std::memory_order_acquire) == Bake::baking) {
    return;  // GraceUI's thread is making the atlas on this device: the next frame looks again
  }
  const bool removed = g.device->GetDeviceRemovedReason() != S_OK;
  if (!removed && g.fence != nullptr && !fence_passed(g.fence_last)) {
    return;  // GraceUI's last frames are still on the GPU
  }
  try {
    release_device_objects();
  } catch (...) {
    log_line("letting go of the game's device failed (an exception): GraceUI holds it still");
    g.teardown = false;
    return;
  }
  g.teardown = false;
  log_line("GraceUI let go of the game's Direct3D 12 device and everything it made on it");
}

void begin_teardown(const char* why) noexcept {
  turn_off(why);
  g.teardown = true;
  try_teardown();
}

#if defined(GRACEUI_TEST_EXPORTS)
constinit std::atomic<bool> g_test_turn_off{false};
#endif

}  // namespace

void bind(IDXGISwapChain3* swap_chain, ID3D12Device* device, const DXGI_SWAP_CHAIN_DESC1& desc, HWND window) {
  if (g.off) {
    device->Release();  // the render path is off for the session: nothing is made on any device again
    return;
  }
  if (g.device != nullptr && g.device != device) {
    // GraceUI 0.1 draws on the device of the first swap chain it binds; a later version rebuilds on a new one. The old
    // device and everything made on it are let go (L3).
    device->Release();
    begin_teardown("the game's Direct3D 12 device changed");
    return;
  }
  if (g.device == nullptr) {
    g.device = device;
  } else {
    device->Release();  // the same device: one reference is enough
  }
  if (g.ready && desc.Format != g.format) {
    g.format_pending = true;
  }
  if (g.ready && window != g.window) {
    ImGui_ImplWin32_Shutdown();
    if (!ImGui_ImplWin32_Init(window)) {
      turn_off("the Win32 backend could not move to the new swap chain's window");
      return;
    }
  }
  g.swap_chain = swap_chain;
  g.window = window;
  g.width = desc.Width;
  g.height = desc.Height;
  display::set_size(g.width, g.height);  // a bound swap chain's size (D180)
  g.buffer_count = desc.BufferCount;
  g.format = desc.Format;
  forget_back_buffers();
  g_buffers_stale.store(false, std::memory_order_relaxed);
  state::bound.store(true, std::memory_order_relaxed);
  state::binds.fetch_add(1, std::memory_order_relaxed);
  log_line("bound a Direct3D 12 swap chain: %ux%u, %u buffers, format %d", desc.Width, desc.Height, desc.BufferCount,
           static_cast<int>(desc.Format));
}

void unbind() noexcept {
  forget_back_buffers();
  g.swap_chain = nullptr;
  retire_queue();  // the next swap chain may present with another queue: it is found again (D116)
  state::bound.store(false, std::memory_order_relaxed);
  log_line("the bound swap chain has ended");
}

bool has_queue() noexcept { return g.queue != nullptr; }

ID3D12Device* device() noexcept { return g.device; }

void set_queue(ID3D12CommandQueue* queue, QueueMethod method) noexcept {
  if (queue == g.queue) {
    queue->Release();
    return;
  }
  retire_queue();
  // The DX12 backend keeps the first queue it was given for its own texture path only, which never runs (D117).
  g.queue = queue;
  state::queue_method.store(method, std::memory_order_relaxed);
  state::reach_frame_step(3);  // D230
  log_line("present queue found: %s", method_name(method));
}

void mark_buffers_stale() noexcept { g_buffers_stale.store(true, std::memory_order_release); }

void render() {
#if defined(GRACEUI_TEST_EXPORTS)
  if (g_test_turn_off.exchange(false, std::memory_order_acq_rel)) {
    turn_off("a test asked for it");
  }
#endif
  if (g.teardown) {
    try_teardown();
  }
  if (g.off || g.swap_chain == nullptr) {
    return;
  }
  if (g.device->GetDeviceRemovedReason() != S_OK) {
    begin_teardown("the game's Direct3D 12 device was removed");
    return;
  }
  if (g_buffers_stale.exchange(false, std::memory_order_acquire)) {
    reread_swap_chain();
  }
  if (g.queue == nullptr) {
    return;
  }
  release_retired_queue_when_done();
  if (g.retired_queue != nullptr) {
    state::frames_skipped_busy.fetch_add(1, std::memory_order_relaxed);
    return;  // nothing on the new present queue until GraceUI's work on the old one has passed (L11)
  }
  if (!g.ready && !warm_up()) {
    return;
  }
  if (!finish_atlas()) {
    return;
  }
  update_textures();  // a mod's texture: drawable once copied, released once its mod has gone, or uploaded now
  if (g.format_pending && !rebuild_for_format()) {
    return;
  }
  if (!g.primed) {
    prime_frame();
  }
  const std::int64_t now_ms = input::now_ms();
  const grace::abi::NoticeList& notices = session::notices().read();
  if (!wants_to_draw(notices, now_ms) || g.width == 0 || g.height == 0) {
    input::rendered();  // the overlay is live with nothing to draw: the input half's watchdog hears it
    return;             // the frame passes through untouched
  }
  Slot& slot = g.slots[g.draws % kSlots];
  if (!fence_passed(slot.fence_value)) {
    // The GPU still has this slot's last frame: skip this one rather than wait (invariant 10). No heartbeat for the
    // watchdog: a holder whose view cannot be drawn for 2 s ends (no_render, L5).
    state::frames_skipped_busy.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const BackBuffer* buffer = back_buffer(g.swap_chain->GetCurrentBackBufferIndex());
  if (buffer == nullptr) {
    return;
  }
  ImDrawData* data = build_frame(notices, now_ms);
  if (data != nullptr && data->TotalVtxCount != 0 && data->DisplaySize.x > 0.0f && data->DisplaySize.y > 0.0f) {
    data->Textures = nullptr;  // D117: the backend's texture path never runs; the atlas is already on the GPU
    record_and_submit(slot, *buffer, data);
  }
  take_back_panels();
  if (!g.off) {
    input::rendered();  // drawn: the input half's watchdog hears it (2 s without one ends a holder)
  }
}

void turn_off(const char* why) noexcept {
  if (!g.off) {
    g.off = true;
    state::render_off.store(true, std::memory_order_release);
    log_line("the render path is off for this session: %s", why);
    // A holder's view can no longer be drawn: its input ends (D100); and with no menu to open, every input passes to
    // the game from now on, the menu key included (L4).
    input::turn_off("the render path is off");
  }
}

void serve_bake() noexcept {
  Bake asked = Bake::asked;
  if (!g_bake.compare_exchange_strong(asked, Bake::baking, std::memory_order_acq_rel)) {
    return;
  }
  bool baked = false;
  try {
    baked = bake_atlas();
  } catch (const ImGuiAssertion& failed) {
    log_line("the shared atlas could not be baked: a Dear ImGui assertion at line %d: %.80s", failed.line,
             failed.expression);
  } catch (...) {
    log_line("the shared atlas could not be baked: an exception");
  }
  g_bake.store(baked ? Bake::done : Bake::failed, std::memory_order_release);
}

#if defined(GRACEUI_TEST_EXPORTS)
void test_turn_off() noexcept { g_test_turn_off.store(true, std::memory_order_release); }
#endif

}  // namespace draw
}  // namespace grace::host
