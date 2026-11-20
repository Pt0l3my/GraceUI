// SPDX-License-Identifier: MIT
// grace-dev: GraceUI's fake host (§9.2, D168). It serves spaces from their page data and fixtures through the real
// grace-serve on loopback, playing their scenarios, until Ctrl+C (or SIGTERM); or, with --dump, prints one page's
// in-game structure and draw list's counts and exits. tools/grace-dev/README.md says how to use it.
//
// Exit status: 0 once stopped (or a dump whose list passes the checker); 1 if the server could not start or stopped
// after a fault (or a dump's list is refused); 2 for a bad argument, space, scenario or web folder.
#include <atomic>
#include <cstdio>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cli.h"
#include "dump.h"
#include "host.h"
#include "json_text.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <signal.h>
#include <time.h>

#include <cerrno>
#endif

namespace {

using grace::dev::Out;

std::mutex g_print;

// Thread-safe: the server's threads and grace-dev's own print through it.
void print(Out level, std::string_view line) {
  std::lock_guard lock(g_print);
  std::FILE* to = level == Out::info ? stdout : stderr;
  const char* prefix = level == Out::error ? "error: " : level == Out::warning ? "warning: " : "";
  std::fputs(prefix, to);
  std::fwrite(line.data(), 1, line.size(), to);
  std::fputc('\n', to);
  std::fflush(to);
}

int failed(std::string_view why, int status = 2) {
  std::lock_guard lock(g_print);
  std::fprintf(stderr, "grace-dev: %.*s\n", static_cast<int>(why.size()), why.data());
  return status;
}

int run_dump(const grace::dev::Cli& cli) {
  grace::dev::SpaceData space;
  std::string why;
  bool found = false;
  for (const std::string& id : grace::dev::builtin_ids()) {
    if (id == cli.dump_space) {
      if (!grace::dev::load_builtin(id, space, why)) {
        return failed(why);
      }
      found = true;
    }
  }
  for (const grace::dev::SpaceSource& source : cli.host.spaces) {
    grace::dev::SpaceData folder;
    if (found || source.folder.empty()) {
      continue;
    }
    if (!grace::dev::load_folder(source.folder, folder, why)) {
      return failed(why);
    }
    if (folder.id == cli.dump_space) {
      space = std::move(folder);
      found = true;
    }
  }
  if (!found) {
    return failed("--dump: there is no space " + cli.dump_space + " (a built-in one, or a --space-dir folder's)");
  }
  grace::dev::DumpResult result;
  if (!grace::dev::dump_page(space, cli.dump, result, why)) {
    return failed(why);
  }
  std::fwrite(result.text.data(), 1, result.text.size(), stdout);
  std::fflush(stdout);
  return result.passes ? 0 : 1;
}

#ifdef _WIN32
std::atomic<bool> g_stop{false};

BOOL WINAPI on_console(DWORD type) {
  if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
    g_stop = true;
    return TRUE;
  }
  return FALSE;
}
#endif

void say_ready(const grace::dev::DevHost& host) {
  const grace::dev::Loaded& loaded = host.loaded();
  std::string spaces;
  for (std::size_t i = 0; i < loaded.spaces.size(); ++i) {
    spaces += (i == 0 ? "" : i + 1 == loaded.spaces.size() ? " and " : ", ") + loaded.spaces[i].id + " (" +
              loaded.spaces[i].pages.name + ")";
  }
  print(Out::info, "grace-dev " GRACE_DEV_VERSION ": serving " + spaces);
  for (const std::optional<grace::dev::Scenario>& scenario : loaded.scenarios) {
    if (scenario.has_value()) {
      print(Out::info, grace::dev::describe(*scenario, host.speed()));
    }
  }
  if (loaded.web_from_folder) {
    print(Out::info, "web: the files a GraceUI folder's graceui.toml lists");
  } else if (loaded.web_files.size() == 1 && loaded.web_files[0].bytes != nullptr &&
             *loaded.web_files[0].bytes == grace::dev::placeholder_page()) {
    print(Out::info, "web: grace-dev's placeholder page (give the web app with --web DIR)");
  } else {
    print(Out::info, "web: " + std::to_string(loaded.web_files.size()) + " files from a plain folder, from memory");
  }
  if (!loaded.web_skipped.empty()) {
    std::string names;
    for (std::size_t i = 0; i < loaded.web_skipped.size() && i < 5; ++i) {
      names += (i == 0 ? "" : ", ") + loaded.web_skipped[i];
    }
    if (loaded.web_skipped.size() > 5) {
      names += " and " + std::to_string(loaded.web_skipped.size() - 5) + " more";
    }
    print(Out::warning, "web: not served, as grace-serve serves no file of such a name or type: " + names);
  }
  if (!host.wait_files(std::chrono::seconds(5)) || !host.stats().files_ok) {
    print(Out::warning, "web: the files failed their check, so page loads get the notice page");
  }
  const std::string port = std::to_string(host.port());
  print(Out::info, std::string("Ready: http://127.0.0.1:") + port + "/ (127.0.0.1:" + port +
                       (host.stats().ipv6 ? " and [::1]:" + port : std::string()) + ", loopback only)");
  print(Out::info, "Press Ctrl+C to stop.");
}

int run(const std::vector<std::string>& args) {
  grace::dev::Cli cli;
  std::string why;
  if (!grace::dev::parse_cli(args, cli, why)) {
    return failed(why);
  }
  if (cli.mode == grace::dev::Cli::Mode::help) {
    const std::string_view text = grace::dev::usage();
    std::fwrite(text.data(), 1, text.size(), stdout);
    return 0;
  }
  if (cli.mode == grace::dev::Cli::Mode::dump) {
    return run_dump(cli);
  }
#ifndef _WIN32
  // Ctrl+C and SIGTERM are taken by sigtimedwait below, never by a handler: blocked here, before any thread starts, so
  // that every thread inherits the mask.
  sigset_t stops;
  sigemptyset(&stops);
  sigaddset(&stops, SIGINT);
  sigaddset(&stops, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &stops, nullptr);
#else
  SetConsoleCtrlHandler(on_console, TRUE);
#endif
  grace::dev::DevHost host(print);
  if (!host.start(cli.host, why)) {
    const bool server = host.start_result() != grace::serve::StartResult::ok && host.loaded().port != 0;
    return failed(why, server ? 1 : 2);
  }
  say_ready(host);
  bool fault = false;
#ifndef _WIN32
  while (true) {
    const timespec quarter{0, 250000000};
    const int taken = sigtimedwait(&stops, nullptr, &quarter);
    if (taken == SIGINT || taken == SIGTERM) {
      break;
    }
    if (!host.running()) {
      fault = true;
      break;
    }
  }
#else
  while (!g_stop.load()) {
    if (!host.running()) {
      fault = true;
      break;
    }
    Sleep(250);
  }
#endif
  print(Out::info, fault ? "grace-dev: the server stopped after a fault." : "grace-dev: stopping.");
  host.stop();
  return fault ? 1 : 0;
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
  SetConsoleOutputCP(CP_UTF8);
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
    std::string arg(size > 0 ? static_cast<std::size_t>(size) : 0, '\0');
    if (size > 0) {
      WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, arg.data(), size, nullptr, nullptr);
      arg.pop_back();  // the terminating null
    }
    args.push_back(std::move(arg));
  }
  return run(args);
}
#else
int main(int argc, char** argv) { return run(std::vector<std::string>(argv + 1, argv + argc)); }
#endif
