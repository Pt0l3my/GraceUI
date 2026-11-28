# GraceUI's example mods (D85), our own, with invented content; tools/grace-harness runs them. The first: a mod in C
# with a notice. The second: a mod in C++ with a panel drawn by its own headless Dear ImGui, a texture and a face. The
# third: a banner and a debug view opened by a hotkey, as EldenNet's F11 view is (D156). The fourth: a mod in C++ with
# a space of its own, its pages compiled from its space.toml by GraceUI's page tool, its topics and commands (D283).
add_subdirectory(hello-notice)
add_subdirectory(hello-panel)
add_subdirectory(hello-debug-view)
add_subdirectory(hello-space)
