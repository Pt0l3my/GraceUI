// pairing.cpp: the network listener's addresses, the pairing code, paired devices and the pairing page (pairing.h).
#include "grace/serve/pairing.h"

#include "ascii.h"
#include "grace/serve/checks.h"
#include "grace/serve/codec.h"

namespace grace::serve {
namespace {

constexpr std::size_t npos = std::string_view::npos;
constexpr std::uint32_t kDraws = 1000000u;               // 10^6 codes
constexpr std::uint32_t kAccept = 4294000000u;           // the largest multiple of 10^6 below 2^32

}  // namespace

bool private_ipv4(std::span<const std::uint8_t, 4> a) noexcept {
  return a[0] == 10 || (a[0] == 172 && (a[1] & 0xF0u) == 16) || (a[0] == 192 && a[1] == 168);
}

std::size_t format_ipv4(std::span<const std::uint8_t, 4> a, std::span<char, 16> out) noexcept {
  std::size_t n = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    if (i > 0) {
      out[n++] = '.';
    }
    const unsigned v = a[i];
    if (v >= 100) {
      out[n++] = static_cast<char>('0' + v / 100);
    }
    if (v >= 10) {
      out[n++] = static_cast<char>('0' + v / 10 % 10);
    }
    out[n++] = static_cast<char>('0' + v % 10);
  }
  return n;
}

std::array<std::uint8_t, 32> device_hash(std::string_view token) noexcept {
  Sha256 sha;
  sha.update(token);
  return sha.finish();
}

std::optional<std::size_t> match_device(std::optional<std::string_view> cookie_field,
                                        std::span<const DeviceKey> devices, std::string_view* token) noexcept {
  if (token != nullptr) {
    *token = {};
  }
  if (!cookie_field || cookie_field->size() > kMaxCookie || devices.empty()) {
    return std::nullopt;
  }
  std::size_t found = devices.size();
  std::string_view rest = *cookie_field;
  while (true) {
    const std::size_t semicolon = rest.find(';');
    const std::string_view pair = detail::trim_ows(rest.substr(0, semicolon));
    const std::size_t equals = pair.find('=');
    if (equals != npos && detail::trim_ows(pair.substr(0, equals)) == kDeviceCookie) {
      const std::string_view value = detail::trim_ows(pair.substr(equals + 1));
      if (valid_token(value)) {
        const std::array<std::uint8_t, 32> hash = device_hash(value);
        // Every device is compared in full: which one matched, if any, takes no shorter time.
        for (std::size_t i = 0; i < devices.size(); ++i) {
          unsigned difference = 0;
          for (std::size_t b = 0; b < hash.size(); ++b) {
            difference |= static_cast<unsigned>(hash[b] ^ devices[i].hash[b]);
          }
          if (difference == 0 && found == devices.size()) {
            found = i;
            if (token != nullptr) {
              *token = value;
            }
          }
        }
      }
    }
    if (semicolon == npos) {
      break;
    }
    rest.remove_prefix(semicolon + 1);
  }
  return found < devices.size() ? std::optional<std::size_t>(found) : std::nullopt;
}

bool valid_device_name(std::string_view name) noexcept {
  if (name.empty() || !valid_utf8(name)) {
    return false;
  }
  std::size_t characters = 0;
  bool printable = false;
  for (const char ch : name) {
    const auto byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7F) {
      return false;  // a control character
    }
    if ((byte & 0xC0u) != 0x80u) {
      ++characters;
    }
    printable = printable || byte != ' ';
  }
  return printable && characters <= kMaxDeviceName;
}

bool valid_device_id(std::string_view id) noexcept {
  if (id.size() != kDeviceIdChars) {
    return false;
  }
  for (const char ch : id) {
    if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) {
      return false;
    }
  }
  return true;
}

bool PairingCode::start(std::span<const std::uint8_t> random, Clock::time_point now) noexcept {
  live_ = false;
  wrong_ = 0;
  for (std::size_t at = 0; at + 4 <= random.size(); at += 4) {
    const std::uint32_t draw = static_cast<std::uint32_t>(random[at]) << 24 |
                               static_cast<std::uint32_t>(random[at + 1]) << 16 |
                               static_cast<std::uint32_t>(random[at + 2]) << 8 | static_cast<std::uint32_t>(random[at + 3]);
    if (draw >= kAccept) {
      continue;  // rejected, so that every code is as likely as any other
    }
    std::uint32_t value = draw % kDraws;
    for (std::size_t i = kCodeDigits; i-- > 0;) {
      code_[i] = static_cast<char>('0' + value % 10);
      value /= 10;
    }
    live_ = true;
    expires_ = now + kCodeLife;
    return true;
  }
  return false;
}

PairingCode::Try PairingCode::attempt(std::string_view code, Clock::time_point now) noexcept {
  if (!showing(now)) {
    live_ = false;
    return Try::none;
  }
  if (constant_time_equal(code, std::string_view(code_.data(), code_.size()))) {
    cancel();  // once
    return Try::paired;
  }
  if (++wrong_ >= kCodeTries) {
    cancel();  // void after five wrong tries
  }
  return Try::wrong;
}

bool PairingCode::showing(Clock::time_point now) const noexcept { return live_ && now < expires_; }

std::string_view PairingCode::code(Clock::time_point now) const noexcept {
  return showing(now) ? std::string_view(code_.data(), code_.size()) : std::string_view();
}

void PairingCode::cancel() noexcept {
  live_ = false;
  wrong_ = 0;
  code_.fill('0');
}

std::string_view pairing_page() noexcept {
  static constexpr std::string_view kPage =
      "<!DOCTYPE html>\n"
      "<html lang=\"en-GB\">\n"
      "<head>\n"
      "<meta charset=\"utf-8\">\n"
      "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
      "<title>Pair this device: GraceUI</title>\n"
      "<link rel=\"stylesheet\" href=\"/pair.css\">\n"
      "<script src=\"/pair.js\" defer></script>\n"
      "</head>\n"
      "<body>\n"
      "<main>\n"
      "<h1>Pair this device</h1>\n"
      "<p>In game, open GraceUI's Browser view page and choose Pair a device. Enter the six-digit code it shows, and a "
      "name for this device. The code works once, for two minutes.</p>\n"
      "<form id=\"pair\" autocomplete=\"off\" novalidate>\n"
      "<label for=\"code\">Code</label>\n"
      "<input id=\"code\" name=\"code\" inputmode=\"numeric\" maxlength=\"6\" pattern=\"[0-9]{6}\" required>\n"
      "<label for=\"name\">This device's name</label>\n"
      "<input id=\"name\" name=\"name\" maxlength=\"32\" placeholder=\"Phone\" required>\n"
      "<button type=\"submit\">Pair</button>\n"
      "</form>\n"
      "<p id=\"said\" role=\"status\" aria-live=\"polite\"></p>\n"
      "</main>\n"
      "</body>\n"
      "</html>\n";
  return kPage;
}

std::string_view pairing_script() noexcept {
  static constexpr std::string_view kScript =
      "\"use strict\";\n"
      "document.addEventListener(\"DOMContentLoaded\", () => {\n"
      "  const form = document.getElementById(\"pair\");\n"
      "  const said = document.getElementById(\"said\");\n"
      "  form.addEventListener(\"submit\", async (event) => {\n"
      "    event.preventDefault();\n"
      "    const code = document.getElementById(\"code\").value.trim();\n"
      "    const name = document.getElementById(\"name\").value.trim();\n"
      "    said.textContent = \"\";\n"
      "    try {\n"
      "      const answer = await fetch(\"/api/pair\", {\n"
      "        method: \"POST\", credentials: \"same-origin\", cache: \"no-store\",\n"
      "        headers: { \"Content-Type\": \"application/json\" },\n"
      "        body: JSON.stringify({ code, name }),\n"
      "      });\n"
      "      if (answer.ok) {\n"
      "        location.replace(\"/\");\n"
      "        return;\n"
      "      }\n"
      "      said.textContent = (await answer.text()).slice(0, 200);\n"
      "    } catch {\n"
      "      said.textContent = \"The game did not answer: is it still running, with home network access on?\";\n"
      "    }\n"
      "  });\n"
      "});\n";
  return kScript;
}

std::string_view pairing_style() noexcept {
  static constexpr std::string_view kStyle =
      ":root { color-scheme: dark; }\n"
      "body { margin: 0; background: #100E0B; color: #CFC6B1; font: 16px/1.5 system-ui, sans-serif; }\n"
      "main { max-width: 28rem; margin: 3rem auto; padding: 1.5rem; background: #1D1A15; border: 1px solid #7E7460; "
      "border-radius: 8px; }\n"
      "h1 { margin: 0 0 0.75rem; color: #C9A65A; font-size: 1.5rem; font-weight: 500; letter-spacing: 0.04em; }\n"
      "p { margin: 0 0 1rem; }\n"
      "form { display: grid; gap: 0.5rem; }\n"
      "label { color: #E8E2D0; }\n"
      "input { padding: 0.5rem; background: #0B0A08; color: #E8E2D0; border: 1px solid #7E7460; border-radius: 4px; "
      "font: inherit; }\n"
      "input:focus-visible, button:focus-visible { outline: 2px solid #C9A65A; outline-offset: 2px; }\n"
      "button { margin-top: 0.5rem; padding: 0.6rem; background: #C9A65A; color: #15120C; border: 0; border-radius: 4px; "
      "font: inherit; font-weight: 600; cursor: pointer; }\n"
      "#said { margin-top: 1rem; color: #D9806F; min-height: 1.5em; }\n";
  return kStyle;
}

}  // namespace grace::serve
