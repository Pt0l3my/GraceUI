// grace-protocol (D159): the golden frames (each taken frame encoded again byte for byte, each refused one with its
// code), a round trip of every kind, the framework's commands, the refusals no text file can hold, and the cost of the
// largest frame against §8's budget (printed: S4 measures it in game).
#include <doctest/doctest.h>

#include <chrono>
#include <fstream>
#include <string>
#include <vector>

#include "grace/protocol/frame.h"

namespace {

using namespace grace::protocol;

struct Golden {
  Direction direction = Direction::from_page;
  std::string expect;
  std::string frame;
};

std::vector<Golden> golden() {
  std::vector<Golden> out;
  std::ifstream file(GRACE_PROTOCOL_GOLDEN);
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty()) {
      continue;
    }
    const nlohmann::json entry = nlohmann::json::parse(line);
    out.push_back({entry.at("dir").get<std::string>() == "page" ? Direction::from_page : Direction::to_page,
                   entry.at("expect").get<std::string>(), entry.at("frame").get<std::string>()});
  }
  return out;
}

Message parsed(std::string_view text, Direction direction) {
  Message message;
  Refusal refusal;
  REQUIRE_MESSAGE(parse_frame(text, direction, message, refusal), refusal.where.field, ": ", refusal.where.why);
  return message;
}

ErrorCode refused(std::string_view text, Direction direction) {
  Message message;
  Refusal refusal;
  CHECK_FALSE(parse_frame(text, direction, message, refusal));
  return refusal.code;
}

}  // namespace

TEST_CASE("the golden frames: each taken one encodes again byte for byte, each refused one with its code") {
  const std::vector<Golden> frames = golden();
  REQUIRE(frames.size() >= 30);
  int taken = 0;
  for (const Golden& g : frames) {
    CAPTURE(g.frame);
    Message message;
    Refusal refusal;
    const bool ok = parse_frame(g.frame, g.direction, message, refusal);
    if (g.expect == "ok") {
      REQUIRE_MESSAGE(ok, refusal.where.field, ": ", refusal.where.why);
      std::string again;
      REQUIRE(encode_frame(message, g.direction, again));
      CHECK(again == g.frame);
      ++taken;
    } else {
      CHECK_FALSE(ok);
      CHECK(to_string(refusal.code) == g.expect);
      CHECK(refusal.where.why.find(g.frame) == std::string::npos);  // never the frame itself
    }
  }
  CHECK(taken == 15);
}

TEST_CASE("every kind survives its own encoding") {
  std::vector<Message> messages;
  messages.push_back(Hello{"1.0", "grace-dev 0.2.0", {"settings"}, std::nullopt});
  SpaceInfo demo{"demo", "Demo space", "0.2.0", "0.2", {1, "ab12"}};
  messages.push_back(Welcome{"1.0", {}, "0.2.0", {demo}, "s-1"});
  messages.push_back(Sub{"demo", 1, "settings", std::nullopt});
  messages.push_back(Unsub{"demo", 1, "settings"});
  messages.push_back(State{"demo", "settings", 7, Json::object({{"a", 1}})});
  messages.push_back(Rows{"demo", "lantern.log", 3, {Json::object({{"line", "Lit."}})}, 2});
  messages.push_back(Cmd{"demo", 9, "menu.close", Json::object()});
  messages.push_back(Result{"demo", 9, true, std::nullopt, std::nullopt, std::nullopt});
  messages.push_back(Result{"demo", 10, std::nullopt, ErrorCode::not_now, std::nullopt,
                            std::string("Not now: the keeper is trimming the wick.")});  // detail, 1.1
  messages.push_back(Error{std::nullopt, ErrorCode::busy, "Already running."});
  messages.push_back(Bye{ByeReason::shutdown});
  messages.push_back(Ping{});
  messages.push_back(Pong{});
  for (const Message& message : messages) {
    const Direction direction = (message.index() == 0 || message.index() == 2 || message.index() == 3 ||
                                 message.index() == 6)
                                    ? Direction::from_page
                                    : Direction::to_page;
    std::string text;
    REQUIRE(encode_frame(message, direction, text));
    CAPTURE(text);
    CHECK(parsed(text, direction) == message);
  }
}

TEST_CASE("a kind that does not travel a way is neither taken nor written that way") {
  std::string text;
  CHECK_FALSE(encode_frame(Hello{"1.0", "b", {}, std::nullopt}, Direction::to_page, text));
  CHECK_FALSE(encode_frame(Bye{ByeReason::fault}, Direction::from_page, text));
  CHECK(encode_frame(Ping{}, Direction::from_page, text));
  CHECK(encode_frame(Ping{}, Direction::to_page, text));
}

TEST_CASE("the settings description's entry parses as §6.9 shows it") {
  const std::string text = R"({"v":1,"t":"state","space":"demo","topic":"settings","rev":1,"body":{}})";
  CHECK(std::holds_alternative<State>(parsed(text, Direction::to_page)));
  const Json entry = Json::parse(R"({"key": "lantern.gap_s", "type": "duration_s", "default": 2.5, "min": 0.5,
    "max": 10, "step": 0.5, "label": "Time between flickers", "help": "The shortest pause between two flickers.",
    "synonyms": ["gap", "delay"], "home": "lantern.basics", "effect": "next_moment", "availability": "live",
    "locked": false})");
  Setting setting;
  DecodeError error;
  REQUIRE_MESSAGE(decode(entry, setting, "", error), error.field, ": ", error.why);
  CHECK(setting.type == SettingType::duration_s);
  CHECK(setting.effect == SettingEffect::next_moment);
  CHECK(setting.max_.has_value());
  CHECK(*setting.max_ == 10.0);
  CHECK(encode(setting) == entry);
  Json bad = entry;
  bad["type"] = "colour";
  CHECK_FALSE(decode(bad, setting, "", error));
  CHECK(error.field == "type");
}

TEST_CASE("the framework's commands and their arguments") {
  Command command{};
  CommandArgs args;
  Refusal refusal;
  Cmd set{"demo", 1, "settings.set", Json::object({{"key", "lantern.brightness"}, {"value", 0.6}})};
  REQUIRE(parse_command(set, command, args, refusal));
  CHECK(command == Command::settings_set);
  CHECK(std::get<SettingsSetArgs>(args).key == "lantern.brightness");
  CHECK(encode_args(args) == set.args);
  Cmd reset{"demo", 2, "settings.reset", Json::object({{"scope", "all"}, {"confirm", true}})};
  REQUIRE(parse_command(reset, command, args, refusal));
  CHECK(std::get<SettingsResetArgs>(args).scope == ResetScopeKind::all);
  Cmd theirs{"demo", 3, "lantern.trim", Json::object()};
  CHECK_FALSE(parse_command(theirs, command, args, refusal));
  CHECK(refusal.code == ErrorCode::unknown_command);
  Cmd wrong{"demo", 4, "settings.set", Json::object({{"key", "Not A Key"}, {"value", 1}})};
  CHECK_FALSE(parse_command(wrong, command, args, refusal));
  CHECK(refusal.code == ErrorCode::invalid_value);
  CHECK(refusal.where.field == "args.key");
  Cmd big{"demo", 5, "settings.set", Json::object({{"key", "a"}, {"value", std::string(2000, 'x')}})};
  CHECK_FALSE(parse_command(big, command, args, refusal));
  CHECK(refusal.where.field == "args.value");
}

TEST_CASE("refusals no text file holds: invalid UTF-8, the size limits, depth") {
  CHECK(refused("{\"v\":1,\"t\":\"ping\",\"x\":\"\xC0\xAF\"}", Direction::from_page) == ErrorCode::bad_message);
  CHECK(refused("{\"v\":1,\"t\":\"ping\",\"x\":\"\xED\xA0\x80\"}", Direction::from_page) == ErrorCode::bad_message);
  const std::string pad(kFrameFromPage, ' ');
  CHECK(refused("{\"v\":1,\"t\":\"ping\"}" + pad, Direction::from_page) == ErrorCode::bad_message);
  CHECK(std::holds_alternative<Ping>(parsed("{\"v\":1,\"t\":\"ping\"}" + pad, Direction::to_page)));
  std::string deep = "{\"v\":1,\"t\":\"ping\",\"x\":";
  deep += std::string(kMaxDepth, '[') + std::string(kMaxDepth, ']') + "}";
  CHECK(refused(deep, Direction::from_page) == ErrorCode::bad_message);
  std::string ok_depth = "{\"v\":1,\"t\":\"ping\",\"x\":";
  ok_depth += std::string(kMaxDepth - 1, '[') + std::string(kMaxDepth - 1, ']') + "}";
  CHECK(std::holds_alternative<Ping>(parsed(ok_depth, Direction::from_page)));
  CHECK(refused("", Direction::from_page) == ErrorCode::bad_message);
  // Brackets inside a string, after an escaped quote, are not nesting.
  std::string in_string = "{\"v\":1,\"t\":\"ping\",\"s\":\"\\\"" + std::string(kMaxDepth + 4, '[') + "\"}";
  CHECK(std::holds_alternative<Ping>(parsed(in_string, Direction::from_page)));
}

TEST_CASE("unknown fields are ignored (§6.4)") {
  const Message message = parsed(R"({"v":1,"t":"sub","space":"demo","id":1,"topic":"settings","colour":"gold"})",
                                 Direction::from_page);
  CHECK(std::get<Sub>(message).topic == "settings");
}

TEST_CASE("the largest frame's cost (§8: 0.5 ms each way for 256 KiB, measured in game at S4)") {
  Json rows = Json::array();
  std::string text;
  // A state payload near 256 KiB: rows of invented figures.
  Json body = Json::object();
  Json kinds = Json::array();
  std::size_t size = 0;
  while (size < 250 * 1024) {
    kinds.push_back(Json::object({{"id", "kind-" + std::to_string(kinds.size())},
                                  {"label", "An invented kind of lantern"},
                                  {"count", static_cast<int>(kinds.size())},
                                  {"tags", Json::array({"lit", "trimmed", "carried"})}}));
    size += kinds.back().dump().size() + 1;
  }
  body["kinds"] = kinds;
  const Message message = State{"demo", "kinds", 1, body};
  constexpr int kRounds = 5;
  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < kRounds; ++i) {
    REQUIRE(encode_frame(message, Direction::to_page, text));
  }
  const auto encoded = std::chrono::steady_clock::now();
  for (int i = 0; i < kRounds; ++i) {
    Message out;
    Refusal refusal;
    REQUIRE(parse_frame(text, Direction::to_page, out, refusal));
  }
  const auto decoded = std::chrono::steady_clock::now();
  const double encode_ms = std::chrono::duration<double, std::milli>(encoded - start).count() / kRounds;
  const double parse_ms = std::chrono::duration<double, std::milli>(decoded - encoded).count() / kRounds;
  MESSAGE("a ", text.size() / 1024, " KiB state frame: encoding ", encode_ms, " ms, parsing ", parse_ms,
          " ms (budget 0.5 ms each, in game; this build's sanitizers, if any, slow it)");
  CHECK(text.size() <= kFrameToPage);
}
