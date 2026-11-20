# GraceUI's faces (§3.4, §3.16, D22, D51, D167): Noto Sans Regular (the notices' face) and SemiBold, Noto Sans Mono
# Regular, Cinzel Regular and Bold, and EB Garamond Regular, Medium and SemiBold, OFL, each from its publisher's own
# repository at a pinned commit, checked against its SHA-256 and downloaded at configure time into the build tree; the
# repository keeps no font. NOTICE credits them. The build puts them where the harness's game folder has them: GraceUI's
# in `bin\fonts\`, beside graceui.dll, as GraceUI's own folder holds them (D51), with each family's licence (Noto's as
# OFL.txt, the others' as Cinzel-OFL.txt and EBGaramond-OFL.txt); and Noto Sans Mono in `bin\hello-panel\` too, beside
# the example mod that registers it. Cinzel publishes no static 500 or 600: the menu takes Regular and Bold (D167).
include(${CMAKE_CURRENT_LIST_DIR}/fetch.cmake)

set(GU_NOTO_COMMIT 983116fbb1e1c231cf33f1f4ec0f9456055fe1e1)  # notofonts/notofonts.github.io, its main on 2026-11-16
set(GU_NOTO_DIR ${CMAKE_BINARY_DIR}/_deps/noto)
gu_fetch(notofonts/notofonts.github.io ${GU_NOTO_COMMIT} fonts/LICENSE ${GU_NOTO_DIR}
  f2095b08bed08b23a6fe26112fcd679a2bee3f002eef077eb05d215ed1051bd8)
gu_fetch(notofonts/notofonts.github.io ${GU_NOTO_COMMIT} fonts/NotoSans/unhinted/ttf/NotoSans-Regular.ttf ${GU_NOTO_DIR}
  f3961a9cde016d41a4879aecda1474d3a36d6bf54fa0e4643de029cc2248b0e8)
gu_fetch(notofonts/notofonts.github.io ${GU_NOTO_COMMIT} fonts/NotoSansMono/unhinted/ttf/NotoSansMono-Regular.ttf
  ${GU_NOTO_DIR} 87f8ce0522a6c99b743ee5fc75b4073cfdd575639119672828b7b9944b65b4f4)

gu_fetch(notofonts/notofonts.github.io ${GU_NOTO_COMMIT} fonts/NotoSans/unhinted/ttf/NotoSans-SemiBold.ttf ${GU_NOTO_DIR}
  a7af8d4a11e8e49fea9a19786a6469210b512cd4061413e8036bdd2425722795)

set(GU_CINZEL_COMMIT dd598495b0fb2ad84270d5cc75d642d2f1e8eabf)  # NDISCOVER/Cinzel, its default branch on 2026-11-20
set(GU_CINZEL_DIR ${CMAKE_BINARY_DIR}/_deps/cinzel)
gu_fetch(NDISCOVER/Cinzel ${GU_CINZEL_COMMIT} OFL.txt ${GU_CINZEL_DIR}
  a46624198eeb4c2e442c38b0ff3bd8f52caeefd76675c36f410e2fb69014a239)
gu_fetch(NDISCOVER/Cinzel ${GU_CINZEL_COMMIT} fonts/ttf/Cinzel-Regular.ttf ${GU_CINZEL_DIR}
  af0031129f27dc752e8629a80b793d27abea94027faa27cc660c3fc33f607a1f)
gu_fetch(NDISCOVER/Cinzel ${GU_CINZEL_COMMIT} fonts/ttf/Cinzel-Bold.ttf ${GU_CINZEL_DIR}
  0c23ec565db45c5508ee95889c60ad87debd167ca07167a43a5d68572b4e2eac)

set(GU_GARAMOND_COMMIT 106a4a6d377987459ae5e68673a4570f13b957fb)  # octaviopardo/EBGaramond12, on 2026-11-20
set(GU_GARAMOND_DIR ${CMAKE_BINARY_DIR}/_deps/ebgaramond)
gu_fetch(octaviopardo/EBGaramond12 ${GU_GARAMOND_COMMIT} OFL.txt ${GU_GARAMOND_DIR}
  058611bd968817d532cedeab6acaa055e882d365a88523df00e7917ad7a0f704)
foreach(weight Regular Medium SemiBold)
  if(weight STREQUAL "Regular")
    set(hash 2028dc06d3c130b4761693481436a32a8e35ed500bf58c25c53de004106125b8)
  elseif(weight STREQUAL "Medium")
    set(hash 57f9e2a5881ac1a3a7bf02935ceb604ba0ca21961fb161d7a535bae4f74a52b1)
  else()
    set(hash ebf827a102983972abb2a1a3964afd5a7b79ca04f2ddd9e53917ce623cefdf2c)
  endif()
  gu_fetch(octaviopardo/EBGaramond12 ${GU_GARAMOND_COMMIT} fonts/ttf/EBGaramond-${weight}.ttf ${GU_GARAMOND_DIR} ${hash})
  configure_file(${GU_GARAMOND_DIR}/fonts/ttf/EBGaramond-${weight}.ttf
    ${CMAKE_BINARY_DIR}/bin/fonts/EBGaramond-${weight}.ttf COPYONLY)
endforeach()
configure_file(${GU_GARAMOND_DIR}/OFL.txt ${CMAKE_BINARY_DIR}/bin/fonts/EBGaramond-OFL.txt COPYONLY)
foreach(weight Regular Bold)
  configure_file(${GU_CINZEL_DIR}/fonts/ttf/Cinzel-${weight}.ttf ${CMAKE_BINARY_DIR}/bin/fonts/Cinzel-${weight}.ttf
    COPYONLY)
endforeach()
configure_file(${GU_CINZEL_DIR}/OFL.txt ${CMAKE_BINARY_DIR}/bin/fonts/Cinzel-OFL.txt COPYONLY)
configure_file(${GU_NOTO_DIR}/fonts/NotoSans/unhinted/ttf/NotoSans-SemiBold.ttf
  ${CMAKE_BINARY_DIR}/bin/fonts/NotoSans-SemiBold.ttf COPYONLY)
configure_file(${GU_NOTO_DIR}/fonts/NotoSansMono/unhinted/ttf/NotoSansMono-Regular.ttf
  ${CMAKE_BINARY_DIR}/bin/fonts/NotoSansMono-Regular.ttf COPYONLY)
configure_file(${GU_NOTO_DIR}/fonts/NotoSans/unhinted/ttf/NotoSans-Regular.ttf
  ${CMAKE_BINARY_DIR}/bin/fonts/NotoSans-Regular.ttf COPYONLY)
configure_file(${GU_NOTO_DIR}/fonts/LICENSE ${CMAKE_BINARY_DIR}/bin/fonts/OFL.txt COPYONLY)
configure_file(${GU_NOTO_DIR}/fonts/NotoSansMono/unhinted/ttf/NotoSansMono-Regular.ttf
  ${CMAKE_BINARY_DIR}/bin/hello-panel/NotoSansMono-Regular.ttf COPYONLY)
configure_file(${GU_NOTO_DIR}/fonts/LICENSE ${CMAKE_BINARY_DIR}/bin/hello-panel/OFL.txt COPYONLY)
