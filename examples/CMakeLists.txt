# GraceUI's example mods (D85), our own, with invented content; tools/grace-harness runs them. The first: a mod in C
# with a notice. The second: a mod in C++ with a panel drawn by its own headless Dear ImGui, a texture and a face. The
# third: a banner and a debug view opened by a hotkey, as EldenNet's F11 view is (D156). The guide's fourth (a C++ mod
# with topics and commands) comes with the bridge protocol's slots.
add_subdirectory(hello-notice)
add_subdirectory(hello-panel)
add_subdirectory(hello-debug-view)
