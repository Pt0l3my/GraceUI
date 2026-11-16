/* hooks_vtable.h: the C helper that reads the render half's hook targets from the vtables (hooks_vtable.c). */
#ifndef GRACE_HOST_HOOKS_VTABLE_H
#define GRACE_HOST_HOOKS_VTABLE_H

#ifdef __cplusplus
extern "C" {
#endif

/* One function type for every target: each is only ever handed to MinHook, never called through it. */
typedef void (*grace_host_fn)(void);

/* The render half's hooks, in hooks.cpp's order (D102's published list). */
enum {
  GRACE_HOST_HOOK_PRESENT = 0,
  GRACE_HOST_HOOK_PRESENT1 = 1,
  GRACE_HOST_HOOK_RESIZE_BUFFERS = 2,
  GRACE_HOST_HOOK_RESIZE_BUFFERS1 = 3,
  GRACE_HOST_HOOK_EXECUTE_COMMAND_LISTS = 4,
  GRACE_HOST_RENDER_HOOKS = 5
};

/* Fills `targets` and answers 1, or answers 0 and fills nothing it could not read. Runs on the init thread only. */
int grace_host_read_hook_targets(grace_host_fn targets[GRACE_HOST_RENDER_HOOKS]);

#ifdef __cplusplus
}
#endif

#endif /* GRACE_HOST_HOOKS_VTABLE_H */
