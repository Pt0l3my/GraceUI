# GraceUI's notices' face and the second example mod's face (§3.4, D22, D51): Noto Sans Regular and Noto Sans Mono
# Regular, OFL, from the Noto project's own repository at a pinned commit, each file checked against its SHA-256 and
# downloaded at configure time into the build tree; the repository keeps no font. NOTICE credits them. The build puts
# them where the harness's game folder has them: Noto Sans in `bin\fonts\`, beside graceui.dll, as GraceUI's own folder
# holds it (D51), and Noto Sans Mono in `bin\hello-panel\`, beside the example mod that registers it. Each folder
# carries the licence as OFL.txt.
include(${CMAKE_CURRENT_LIST_DIR}/fetch.cmake)

set(GU_NOTO_COMMIT 983116fbb1e1c231cf33f1f4ec0f9456055fe1e1)  # notofonts/notofonts.github.io, its main on 2026-11-16
set(GU_NOTO_DIR ${CMAKE_BINARY_DIR}/_deps/noto)
gu_fetch(notofonts/notofonts.github.io ${GU_NOTO_COMMIT} fonts/LICENSE ${GU_NOTO_DIR}
  f2095b08bed08b23a6fe26112fcd679a2bee3f002eef077eb05d215ed1051bd8)
gu_fetch(notofonts/notofonts.github.io ${GU_NOTO_COMMIT} fonts/NotoSans/unhinted/ttf/NotoSans-Regular.ttf ${GU_NOTO_DIR}
  f3961a9cde016d41a4879aecda1474d3a36d6bf54fa0e4643de029cc2248b0e8)
gu_fetch(notofonts/notofonts.github.io ${GU_NOTO_COMMIT} fonts/NotoSansMono/unhinted/ttf/NotoSansMono-Regular.ttf
  ${GU_NOTO_DIR} 87f8ce0522a6c99b743ee5fc75b4073cfdd575639119672828b7b9944b65b4f4)

configure_file(${GU_NOTO_DIR}/fonts/NotoSans/unhinted/ttf/NotoSans-Regular.ttf
  ${CMAKE_BINARY_DIR}/bin/fonts/NotoSans-Regular.ttf COPYONLY)
configure_file(${GU_NOTO_DIR}/fonts/LICENSE ${CMAKE_BINARY_DIR}/bin/fonts/OFL.txt COPYONLY)
configure_file(${GU_NOTO_DIR}/fonts/NotoSansMono/unhinted/ttf/NotoSansMono-Regular.ttf
  ${CMAKE_BINARY_DIR}/bin/hello-panel/NotoSansMono-Regular.ttf COPYONLY)
configure_file(${GU_NOTO_DIR}/fonts/LICENSE ${CMAKE_BINARY_DIR}/bin/hello-panel/OFL.txt COPYONLY)
