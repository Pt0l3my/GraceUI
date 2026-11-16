# grace-abi: the public C ABI, whose source is its header, graceui.h (D109).
add_library(grace_abi INTERFACE)
target_include_directories(grace_abi INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
