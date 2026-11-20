# The in-game spikes' tools of GraceUI's (S0-E and S4, D175), in the Windows test build only and never released:
# grace-spike.dll, the observer that EldenNet's launcher loads beside a test build of graceui.dll in a spike run and
# that writes the run's report (grace_spike.cpp); and grace-standin.dll, a stand-in for another overlay that patches
# Present with its own code (standin.c). report.py reads the reports; the harness's "spike" scenario checks both DLLs.
add_library(grace_spike SHARED grace_spike.cpp)
set_target_properties(grace_spike PROPERTIES PREFIX "" OUTPUT_NAME grace-spike)
target_include_directories(grace_spike PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(grace_spike PRIVATE grace_abi graceui_sys gu_imgui_core)
target_compile_definitions(grace_spike PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
gu_own_code(grace_spike)

add_library(grace_standin SHARED standin.c)
set_target_properties(grace_standin PROPERTIES PREFIX "" OUTPUT_NAME grace-standin)
target_compile_definitions(grace_standin PRIVATE WIN32_LEAN_AND_MEAN)
gu_own_code(grace_standin)
