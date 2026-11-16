// The anti-cheat check's matching (anticheat.h): the module clause by prefix and the parent clause by name, ASCII
// letters without their case, nothing else.
#include <doctest/doctest.h>

#include "grace/abi/anticheat.h"

using namespace grace::abi;

TEST_CASE("the module clause: a file name that starts with the prefix, in any case") {
  CHECK(anti_cheat_module(u"EasyAntiCheat.dll"));
  CHECK(anti_cheat_module(u"easyanticheat_x64.dll"));
  CHECK(anti_cheat_module(u"EASYANTICHEAT_EOS.DLL"));
  CHECK(anti_cheat_module(u"EasyAntiCheat"));
  CHECK_FALSE(anti_cheat_module(u"EasyAntiChea.dll"));    // one letter short
  CHECK_FALSE(anti_cheat_module(u"NotEasyAntiCheat.dll"));  // the prefix inside, not at the start
  CHECK_FALSE(anti_cheat_module(u"graceui.dll"));
  CHECK_FALSE(anti_cheat_module(u"eldenring.exe"));
  CHECK_FALSE(anti_cheat_module(u""));
  // Only ASCII letters fold: a fullwidth letter is another letter.
  CHECK_FALSE(anti_cheat_module(u"ＥasyAntiCheat.dll"));
}

TEST_CASE("the parent clause: the image name exactly, in any case") {
  CHECK(anti_cheat_parent(u"start_protected_game.exe"));
  CHECK(anti_cheat_parent(u"START_PROTECTED_GAME.EXE"));
  CHECK_FALSE(anti_cheat_parent(u"start_protected_game.exe.bak"));
  CHECK_FALSE(anti_cheat_parent(u"start_protected_game"));
  CHECK_FALSE(anti_cheat_parent(u"steam.exe"));
  CHECK_FALSE(anti_cheat_parent(u""));
}

TEST_CASE("a test build's names: the same rules for other names") {
  CHECK(anti_cheat_module(u"grace-harness-shield.dll", u"GRACE-HARNESS-SHIELD"));
  CHECK_FALSE(anti_cheat_module(u"EasyAntiCheat.dll", u"grace-harness-shield"));
  CHECK(anti_cheat_parent(u"Grace-Harness.exe", u"grace-harness.exe"));
  CHECK_FALSE(anti_cheat_module(u"anything.dll", u""));  // an empty prefix matches nothing
  CHECK_FALSE(anti_cheat_parent(u"", u""));
}

static_assert(anti_cheat_module(u"EasyAntiCheat_EOS.dll"));
static_assert(!anti_cheat_parent(u"eldenring.exe"));
