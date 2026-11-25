# web/: the browser view's checks under CTest (D166; web/README.md). The licence tool's own tests always run; the rest
# run where Node and the web app's packages are installed (npm ci --ignore-scripts in web/): the type check, the logic
# tests, the build, the licence check that writes the bundle's licence file, the bundle scan and, where Playwright and
# its Chromium are already on the machine (never installed by this build), the DOM tests against the built page.
find_package(Python3 3.11 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME web_licences_tool_tests
           COMMAND ${Python3_EXECUTABLE} -I -m unittest discover -s ${CMAKE_SOURCE_DIR}/tools/web)
endif()

find_program(GU_NODE node)
set(GU_WEB ${CMAKE_CURRENT_SOURCE_DIR})
if(GU_NODE AND EXISTS ${GU_WEB}/node_modules/vite/package.json AND EXISTS ${GU_WEB}/node_modules/typescript/bin/tsc)
  add_test(NAME web_typecheck COMMAND ${GU_NODE} node_modules/typescript/bin/tsc -p tsconfig.json
           WORKING_DIRECTORY ${GU_WEB})
  add_test(NAME web_unit_tests COMMAND ${GU_NODE} --test "tests/unit/*.test.ts" WORKING_DIRECTORY ${GU_WEB})
  add_test(NAME web_build COMMAND ${GU_NODE} node_modules/vite/bin/vite.js build --logLevel warn
           WORKING_DIRECTORY ${GU_WEB})
  set_tests_properties(web_build PROPERTIES FIXTURES_SETUP web_dist RESOURCE_LOCK web_dist)
  if(Python3_FOUND)
    add_test(NAME web_licences COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/web/licences.py)
    set_tests_properties(web_licences PROPERTIES FIXTURES_REQUIRED web_dist FIXTURES_SETUP web_licensed
                         RESOURCE_LOCK web_dist)
    add_test(NAME web_bundle_scan COMMAND ${GU_NODE} --test tests/dom/bundle.test.ts WORKING_DIRECTORY ${GU_WEB})
    set_tests_properties(web_bundle_scan PROPERTIES FIXTURES_REQUIRED "web_dist;web_licensed" RESOURCE_LOCK web_dist)
    # The tool's own tests check the real tree too, so they wait for the build and its licence file.
    set_tests_properties(web_licences_tool_tests PROPERTIES FIXTURES_REQUIRED "web_dist;web_licensed"
                         RESOURCE_LOCK web_dist)
  endif()
  execute_process(COMMAND ${GU_NODE} -e "require('playwright').chromium.executablePath()"
                  RESULT_VARIABLE GU_NO_PLAYWRIGHT OUTPUT_QUIET ERROR_QUIET)
  if(GU_NO_PLAYWRIGHT EQUAL 0)
    add_test(NAME web_dom_tests COMMAND ${GU_NODE} --test --test-concurrency=1 tests/dom/dom.test.ts
             WORKING_DIRECTORY ${GU_WEB})
    set_tests_properties(web_dom_tests PROPERTIES FIXTURES_REQUIRED web_dist TIMEOUT 600 RESOURCE_LOCK web_dist)
    # The space check (D265) over the demo's fixture folder, as a consumer runs it over its own, against its golden.
    add_test(NAME web_space_check COMMAND ${GU_NODE} tools/check-space.ts ${CMAKE_SOURCE_DIR}/fixtures/demo
             --golden tests/golden/space-demo.txt WORKING_DIRECTORY ${GU_WEB})
    set_tests_properties(web_space_check PROPERTIES FIXTURES_REQUIRED web_dist TIMEOUT 600 RESOURCE_LOCK web_dist)
  endif()
endif()
