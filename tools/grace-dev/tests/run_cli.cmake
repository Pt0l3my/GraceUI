# Runs grace-dev once for CTest: cmake -DPROGRAM=<grace-dev> -DARGS=<arguments joined by |> -DSTATUS=<exit status>
# [-DGOLDEN=<file standard output must equal>] [-DMATCH=<regular expression standard error must match>] -P run_cli.cmake
string(REPLACE "|" ";" _args "${ARGS}")
execute_process(COMMAND ${PROGRAM} ${_args} RESULT_VARIABLE _status OUTPUT_VARIABLE _out ERROR_VARIABLE _err
  TIMEOUT 60)
if(NOT _status STREQUAL "${STATUS}")
  message(FATAL_ERROR "grace-dev exited with ${_status}, not ${STATUS}\n--- stdout\n${_out}\n--- stderr\n${_err}")
endif()
if(DEFINED GOLDEN)
  file(READ ${GOLDEN} _want)
  if(NOT _out STREQUAL _want)
    message(FATAL_ERROR "grace-dev's output differs from ${GOLDEN}\n--- stdout\n${_out}")
  endif()
endif()
if(DEFINED MATCH AND NOT _err MATCHES "${MATCH}")
  message(FATAL_ERROR "grace-dev's errors do not say \"${MATCH}\"\n--- stderr\n${_err}")
endif()
