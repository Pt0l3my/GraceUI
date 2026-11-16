// draw.cpp: the render path (D107; invariants 10 and 11). On the first Present of the bound swap chain once its present
// queue is known: Dear ImGui's context, its Win32 backend for the swap chain's window and its DX12 backend for the
// game's device and that queue, and the font atlas, baked and uploaded once (D117). Then, on every frame with anything
// to draw, one command list draws Dear ImGui's draw data onto the current back buffer and is executed on the present
// queue before the original Present. It never waits for the GPU: a frame slot still in use is skipped and counted
// (D119). After warm-up it allocates nothing per frame.
//
// SAFETY: the raw pointers here follow three rules. (1) Every COM object this file creates or is handed with a
// reference (the device, the queue, its own heaps, fence, allocators, lists and textures) is owned by one field and
// released once, through release() below. (2) The swap chain and its back buffers are not owned (D118): hooks.cpp binds
// a swap chain only with a live tag and calls unbind() once the tag says the swap chain has ended, and
// mark_buffers_stale() before ResizeBuffers replaces the buffers, which render() takes before it touches one, so no
// pointer into them is used after them. (3) Dear ImGui's objects (context, draw data, atlas texture) are used only
// between their creation and the next call that may replace them, on this thread.
#include "draw.h"

#include <atomic>
#include <cstdint>
#include <cstring>

#include "counters.h"
#include "draw_alloc.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#include "imgui_internal.h"  // ImFontAtlasBuildMain: the atlas baked once, before the first frame (D117)
#include "input.h"
#include "log.h"

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
constexpr UINT kSrvDescriptors = 16;  // the atlas first; room for the textures S10.4.c registers

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
  ID3D12CommandAllocator* atlas_allocator;
  ID3D12GraphicsCommandList* atlas_list;
  UINT64 atlas_fence;
  bool atlas_ready;
  D3D12_GPU_DESCRIPTOR_HANDLE atlas_gpu;

  std::int64_t qpc_frequency;
  std::int64_t qpc_last;
};

constinit Renderer g{};

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

void retire_queue() noexcept {
  if (g.queue == nullptr) {
    return;
  }
  if (g.retired_queue != nullptr) {
    if (fence_passed(g.retired_after)) {
      release(g.retired_queue);
    } else {
      // Two queues retired before the first's work was done: keep the newer, and leave the older's one reference.
      log_line("a retired present queue still had work on the GPU; its reference is left");
      g.retired_queue = nullptr;
    }
  }
  g.retired_queue = g.queue;
  g.retired_after = g.fence_last;
  g.queue = nullptr;
  state::queue_method.store(QueueMethod::none, std::memory_order_relaxed);
}

void release_retired_queue_when_done() noexcept {
  if (g.retired_queue != nullptr && fence_passed(g.retired_after)) {
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

// The atlas (D117): Dear ImGui bakes it once, here, and GraceUI uploads it itself, once, on the present queue, and
// polls its fence on later frames: the DX12 backend's own texture path would wait for the GPU on this thread.
bool start_atlas() {
  ImFontAtlas* atlas = ImGui::GetIO().Fonts;
  // GraceUI 0.1 bakes Dear ImGui's own small font only; the Grace faces and those consumers register join the same
  // bake before warm-up (S10.4.c, §3.16).
  if (atlas->AddFontDefaultBitmap() == nullptr) {
    return false;
  }
  ImFontAtlasBuildMain(atlas);
  ImTextureData* texture = atlas->TexData;
  if (texture == nullptr || texture->Pixels == nullptr || texture->Format != ImTextureFormat_RGBA32) {
    return false;
  }
  const UINT width = static_cast<UINT>(texture->Width);
  const UINT height = static_cast<UINT>(texture->Height);

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
  if (FAILED(g.device->CreateCommittedResource(&device_heap, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&g.atlas)))) {
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
  if (FAILED(g.device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&g.atlas_upload)))) {
    return false;
  }
  void* mapped = nullptr;
  const D3D12_RANGE nothing_read{0, 0};
  if (FAILED(g.atlas_upload->Map(0, &nothing_read, &mapped))) {
    return false;
  }
  unsigned char* const rows = static_cast<unsigned char*>(mapped) + footprint.Offset;
  const unsigned char* const pixels = texture->Pixels;
  const std::size_t row_bytes = static_cast<std::size_t>(width) * 4;
  for (UINT y = 0; y < height; ++y) {
    std::memcpy(rows + static_cast<std::size_t>(y) * footprint.Footprint.RowPitch, pixels + y * row_bytes, row_bytes);
  }
  const D3D12_RANGE written{0, static_cast<SIZE_T>(upload_bytes)};
  g.atlas_upload->Unmap(0, &written);

  D3D12_SHADER_RESOURCE_VIEW_DESC view{};
  view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  view.Texture2D.MipLevels = 1;
  g.device->CreateShaderResourceView(g.atlas, &view, g.srv_cpu_start);
  g.atlas_gpu = g.srv_gpu_start;
  g.srv_used = 1;

  // The copy runs on the present queue, so every later frame of ours, on the same queue, comes after it.
  if (FAILED(g.atlas_allocator->Reset()) || FAILED(g.atlas_list->Reset(g.atlas_allocator, nullptr))) {
    return false;
  }
  D3D12_TEXTURE_COPY_LOCATION target{};
  target.pResource = g.atlas;
  target.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  target.SubresourceIndex = 0;
  D3D12_TEXTURE_COPY_LOCATION source{};
  source.pResource = g.atlas_upload;
  source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  source.PlacedFootprint = footprint;
  g.atlas_list->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
  transition(g.atlas_list, g.atlas, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  if (FAILED(g.atlas_list->Close())) {
    return false;
  }
  ID3D12CommandList* const lists[] = {g.atlas_list};
  g.queue->ExecuteCommandLists(1, lists);
  if (FAILED(g.queue->Signal(g.fence, ++g.fence_last))) {
    return false;
  }
  g.atlas_fence = g.fence_last;
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
  release(g.atlas_list);
  release(g.atlas_allocator);
  g.atlas_ready = true;
  state::atlas_ready.store(true, std::memory_order_relaxed);
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
  if (FAILED(g.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g.atlas_allocator))) ||
      FAILED(g.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g.atlas_allocator, nullptr,
                                         IID_PPV_ARGS(&g.atlas_list))) ||
      FAILED(g.atlas_list->Close())) {
    return false;
  }

#if defined(GRACEUI_TEST_EXPORTS)
  ImGui::SetAllocatorFunctions(&draw_alloc::imgui_alloc, &draw_alloc::imgui_free, nullptr);
#endif
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;  // Dear ImGui writes no file (D84: GraceUI writes only under its user folder)
  io.LogFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;  // the cursor is the input half's (§5.5)
  io.ConfigErrorRecoveryEnableTooltip = false;              // nothing of Dear ImGui's own is ever drawn on the game
  if (!ImGui_ImplWin32_Init(g.window) || !init_dx12_backend() || !start_atlas()) {
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
  g.buffer_count = desc.BufferCount;
  if (desc.Format != g.format) {
    g.format = desc.Format;
    g.format_pending = g.ready;
  }
}

bool wants_to_draw() noexcept {
  // GraceUI 0.1 draws nothing of its own; panels, notices and the menu join here with S10.4.c.
  return state::test_panel.load(std::memory_order_relaxed);
}

void draw_test_panel() {
#if defined(GRACEUI_TEST_EXPORTS)
  constexpr ImU32 panel = (255u << IM_COL32_A_SHIFT) | (unsigned{GRACEUI_TEST_PANEL_B} << IM_COL32_B_SHIFT) |
                          (unsigned{GRACEUI_TEST_PANEL_G} << IM_COL32_G_SHIFT) |
                          (unsigned{GRACEUI_TEST_PANEL_R} << IM_COL32_R_SHIFT);
  constexpr ImU32 white = 0xFFFFFFFFu;
  ImDrawList* list = ImGui::GetForegroundDrawList();
  list->AddRectFilled(ImVec2(float{GRACEUI_TEST_PANEL_X0}, float{GRACEUI_TEST_PANEL_Y0}),
                      ImVec2(float{GRACEUI_TEST_PANEL_X1}, float{GRACEUI_TEST_PANEL_Y1}), panel);
  list->AddText(ImVec2(float{GRACEUI_TEST_TEXT_X}, float{GRACEUI_TEST_TEXT_Y}), white, GRACEUI_TEST_TEXT);
#endif
}

ImDrawData* build_frame() {
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(static_cast<float>(g.width), static_cast<float>(g.height));
  io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
  const std::int64_t now = qpc_now();
  const double seconds = static_cast<double>(now - g.qpc_last) / static_cast<double>(g.qpc_frequency);
  io.DeltaTime = seconds > 1e-6 ? static_cast<float>(seconds) : 1e-6f;
  g.qpc_last = now;
  ImGui_ImplDX12_NewFrame();
  input::before_imgui_frame();  // the window's input, while a holder has it (the input half; D77, D122)
  ImGui::NewFrame();
  draw_test_panel();
  ImGui::Render();
  return ImGui::GetDrawData();
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
}

}  // namespace

void bind(IDXGISwapChain3* swap_chain, ID3D12Device* device, const DXGI_SWAP_CHAIN_DESC1& desc, HWND window) {
  if (g.device != nullptr && g.device != device) {
    // GraceUI 0.1 keeps the device of the first swap chain it binds; a later version rebuilds on a new one.
    device->Release();
    turn_off("the game's Direct3D 12 device changed");
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
  log_line("present queue found: %s", method_name(method));
}

void mark_buffers_stale() noexcept { g_buffers_stale.store(true, std::memory_order_release); }

void render() {
  if (g.off || g.swap_chain == nullptr) {
    return;
  }
  if (g_buffers_stale.exchange(false, std::memory_order_acquire)) {
    reread_swap_chain();
  }
  if (g.queue == nullptr) {
    return;
  }
  if (!g.ready && !build_renderer()) {
    turn_off("the renderer could not be built on the game's device");
    return;
  }
  release_retired_queue_when_done();
  if (!finish_atlas()) {
    return;
  }
  if (g.format_pending && !rebuild_for_format()) {
    return;
  }
  if (!wants_to_draw() || g.width == 0 || g.height == 0) {
    return;  // nothing to draw: the frame passes through untouched
  }
  Slot& slot = g.slots[g.draws % kSlots];
  if (!fence_passed(slot.fence_value)) {
    state::frames_skipped_busy.fetch_add(1, std::memory_order_relaxed);
    return;  // the GPU still has this slot's last frame: skip this one rather than wait (invariant 10)
  }
  const BackBuffer* buffer = back_buffer(g.swap_chain->GetCurrentBackBufferIndex());
  if (buffer == nullptr) {
    return;
  }
  ImDrawData* data = build_frame();
  if (data == nullptr || data->TotalVtxCount == 0 || data->DisplaySize.x <= 0.0f || data->DisplaySize.y <= 0.0f) {
    return;
  }
  data->Textures = nullptr;  // D117: the backend's texture path never runs; the atlas is already on the GPU
  record_and_submit(slot, *buffer, data);
}

void turn_off(const char* why) noexcept {
  if (!g.off) {
    g.off = true;
    state::render_off.store(true, std::memory_order_relaxed);
    log_line("the render path is off for this session: %s", why);
  }
}

}  // namespace draw
}  // namespace grace::host
