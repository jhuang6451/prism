/**
 * @file desktop_apps.cpp
 * @brief Enumerate installed freedesktop.org GUI applications so Prism can sync them into the app list.
 */
#include "desktop_apps.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>

namespace fs = std::filesystem;
using namespace std::literals;

namespace {

  /**
   * @brief Key/value pairs of a desktop entry's `[Desktop Entry]` group.
   */
  using entry_t = std::map<std::string, std::string, std::less<>>;

  /**
   * @brief Icon directories under `icons/hicolor`, largest first, that Prism can show.
   */
  const std::vector<std::string> ICON_SIZES = {"512x512"s, "256x256"s, "192x192"s, "128x128"s, "96x96"s, "64x64"s, "48x48"s};

  /**
   * @brief Remove leading and trailing blanks.
   *
   * @param text Text to trim.
   * @return The trimmed text.
   */
  std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r"sv);
    if (first == std::string_view::npos) {
      return {};
    }
    const auto last = text.find_last_not_of(" \t\r"sv);
    return std::string {text.substr(first, last - first + 1)};
  }

  /**
   * @brief Convert ASCII text to lowercase.
   *
   * @param text Text to convert.
   * @return The lowercase text.
   */
  std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    return text;
  }

  /**
   * @brief Split text at a separator, dropping empty parts.
   *
   * @param text Text to split.
   * @param separator Separator character.
   * @return The non-empty parts in order.
   */
  std::vector<std::string> split(std::string_view text, char separator) {
    std::vector<std::string> parts;
    while (!text.empty()) {
      const auto end = text.find(separator);
      if (end != 0) {
        parts.emplace_back(text.substr(0, end));
      }
      if (end == std::string_view::npos) {
        break;
      }
      text.remove_prefix(end + 1);
    }
    return parts;
  }

  /**
   * @brief Check whether two lists share an element.
   *
   * @param a First list.
   * @param b Second list.
   * @return True when at least one element is in both lists.
   */
  bool intersects(const std::vector<std::string> &a, const std::vector<std::string> &b) {
    return std::any_of(a.begin(), a.end(), [&b](const std::string &item) {
      return std::find(b.begin(), b.end(), item) != b.end();
    });
  }

  /**
   * @brief Quote an argument for safe use in a POSIX shell command.
   *
   * @param arg Argument to quote.
   * @return The argument unchanged when it only has shell-safe characters, otherwise single-quoted.
   */
  std::string shell_quote(const std::string &arg) {
    constexpr auto safe = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@%+=:,./-_"sv;
    if (!arg.empty() && arg.find_first_not_of(safe) == std::string::npos) {
      return arg;
    }
    std::string quoted {"'"};
    for (const char c : arg) {
      if (c == '\'') {
        quoted += "'\\''";
      } else {
        quoted += c;
      }
    }
    quoted += '\'';
    return quoted;
  }

  /**
   * @brief Split an `Exec` value into arguments using the desktop entry quoting rules.
   *
   * Arguments are separated by blanks. Inside double quotes a backslash escapes
   * `"`, `` ` ``, `$` and `\`.
   *
   * @param exec Raw `Exec` value.
   * @return The unquoted arguments.
   */
  std::vector<std::string> split_exec(std::string_view exec) {
    std::vector<std::string> args;
    std::string current;
    bool in_arg = false;
    bool quoted = false;
    for (std::size_t i = 0; i < exec.size(); ++i) {
      const char c = exec[i];
      if (quoted) {
        if (c == '"') {
          quoted = false;
        } else if (c == '\\' && i + 1 < exec.size() && R"("`$\)"sv.find(exec[i + 1]) != std::string_view::npos) {
          current += exec[++i];
        } else {
          current += c;
        }
      } else if (c == '"') {
        quoted = true;
        in_arg = true;
      } else if (c == ' ' || c == '\t') {
        if (in_arg) {
          args.push_back(std::move(current));
          current.clear();
          in_arg = false;
        }
      } else {
        current += c;
        in_arg = true;
      }
    }
    if (in_arg) {
      args.push_back(std::move(current));
    }
    return args;
  }

  /**
   * @brief Remove the field codes from one `Exec` argument.
   *
   * @param arg Unquoted argument.
   * @param removed Set to true when a field code was dropped.
   * @return The argument with `%%` turned into `%` and every other `%x` code removed.
   */
  std::string strip_field_codes(const std::string &arg, bool &removed) {
    std::string result;
    for (std::size_t i = 0; i < arg.size(); ++i) {
      if (arg[i] != '%' || i + 1 == arg.size()) {
        result += arg[i];
        continue;
      }
      if (const char code = arg[++i]; code == '%') {
        result += '%';
      } else {
        removed = true;
      }
    }
    return result;
  }

  /**
   * @brief Read the `[Desktop Entry]` group of a desktop file.
   *
   * Localized keys such as `Name[de]` are ignored and the first occurrence of a
   * repeated key wins.
   *
   * @param path Desktop file to read.
   * @return The group's keys; empty when the file cannot be read.
   */
  entry_t read_entry(const fs::path &path) {
    entry_t entry;
    std::ifstream in(path);
    std::string line;
    bool in_group = false;
    while (std::getline(in, line)) {
      line = trim(line);
      if (line.empty() || line.front() == '#') {
        continue;
      }
      if (line.front() == '[') {
        in_group = line == "[Desktop Entry]"sv;
        continue;
      }
      const auto equals = line.find('=');
      if (!in_group || equals == std::string::npos) {
        continue;
      }
      if (auto key = trim(std::string_view {line}.substr(0, equals)); key.find('[') == std::string::npos) {
        entry.emplace(std::move(key), trim(std::string_view {line}.substr(equals + 1)));
      }
    }
    return entry;
  }

  /**
   * @brief Look up a key of a desktop entry.
   *
   * @param entry Desktop entry keys.
   * @param key Key to look up.
   * @return The value, or an empty string when the key is absent.
   */
  std::string value(const entry_t &entry, std::string_view key) {
    const auto it = entry.find(key);
    return it == entry.end() ? std::string {} : it->second;
  }

  /**
   * @brief Check whether a path is an executable regular file.
   *
   * @param path Path to check.
   * @return True when the path (after following symlinks) is an executable file.
   */
  bool is_executable_file(const fs::path &path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
      return false;
    }
    const auto permissions = fs::status(path, ec).permissions();
    return !ec && (permissions & (fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec)) != fs::perms::none;
  }

  /**
   * @brief Check whether a `TryExec` program exists.
   *
   * @param program Absolute path, or a program name to look up in `PATH`.
   * @return True when the program is an executable file.
   */
  bool program_exists(const std::string &program) {
    if (program.find('/') != std::string::npos) {
      return is_executable_file(program);
    }
    const char *path = std::getenv("PATH");
    if (path == nullptr) {
      return false;
    }
    const auto dirs = split(path, ':');
    return std::any_of(dirs.begin(), dirs.end(), [&program](const std::string &dir) {
      return is_executable_file(fs::path(dir) / program);
    });
  }

  /**
   * @brief Apply an entry's `OnlyShowIn` and `NotShowIn` lists to the current desktop.
   *
   * @param entry Desktop entry keys.
   * @param desktops Names from `XDG_CURRENT_DESKTOP`.
   * @return True when the entry may be shown in the desktop.
   */
  bool shown_in_desktop(const entry_t &entry, const std::vector<std::string> &desktops) {
    const auto only = split(value(entry, "OnlyShowIn"), ';');
    if (!only.empty() && !intersects(only, desktops)) {
      return false;
    }
    return !intersects(split(value(entry, "NotShowIn"), ';'), desktops);
  }

  /**
   * @brief Check whether a name ends in `.png`, ignoring case.
   *
   * @param name File name or path.
   * @return True for a PNG file name.
   */
  bool has_png_extension(const std::string &name) {
    return lowercase(fs::path(name).extension().string()) == ".png"sv;
  }

  /**
   * @brief Find a PNG for an entry's `Icon` value.
   *
   * An absolute path is used as is. A theme icon name is looked up in the
   * `hicolor` theme (largest size first) and in `pixmaps`. Prism only serves PNG,
   * so SVG-only icons are not found.
   *
   * @param icon `Icon` value.
   * @param dirs Data directories to search.
   * @return Path to the PNG, or an empty path when none was found.
   */
  fs::path find_icon(const std::string &icon, const std::vector<fs::path> &dirs) {
    std::error_code ec;
    if (icon.empty()) {
      return {};
    }
    if (icon.front() == '/') {
      return has_png_extension(icon) && fs::is_regular_file(icon, ec) ? fs::path {icon} : fs::path {};
    }
    if (icon.find('/') != std::string::npos) {
      return {};
    }
    const std::string file = has_png_extension(icon) ? icon : icon + ".png"s;
    for (const auto &dir : dirs) {
      for (const auto &size : ICON_SIZES) {
        if (fs::path candidate = dir / "icons" / "hicolor" / size / "apps" / file; fs::is_regular_file(candidate, ec)) {
          return candidate;
        }
      }
      if (fs::path candidate = dir / "pixmaps" / file; fs::is_regular_file(candidate, ec)) {
        return candidate;
      }
    }
    return {};
  }

  /**
   * @brief List the desktop files below an `applications` directory.
   *
   * @param applications Directory to search recursively.
   * @return Paths of regular `.desktop` files, sorted.
   */
  std::vector<fs::path> desktop_files(const fs::path &applications) {
    std::vector<fs::path> files;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(applications, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
      std::error_code entry_ec;
      if (it->path().extension() == ".desktop"sv && it->is_regular_file(entry_ec)) {
        files.push_back(it->path());
      }
    }
    std::sort(files.begin(), files.end());
    return files;
  }

}  // namespace

namespace prism::desktop {

  std::string exec_command(std::string_view exec) {
    std::string command;
    for (const auto &arg : split_exec(exec)) {
      if (arg == "@@"sv || arg == "@@u"sv) {
        continue;
      }
      bool removed = false;
      const auto stripped = strip_field_codes(arg, removed);
      if (removed && stripped.empty()) {
        continue;
      }
      if (!command.empty()) {
        command += ' ';
      }
      command += shell_quote(stripped);
    }
    return command;
  }

  std::vector<fs::path> data_dirs() {
    std::vector<fs::path> dirs;
    const auto add = [&dirs](fs::path dir) {
      if (!dir.empty() && std::find(dirs.begin(), dirs.end(), dir) == dirs.end()) {
        dirs.push_back(std::move(dir));
      }
    };
    const char *home = std::getenv("HOME");
    const bool have_home = home != nullptr && *home != '\0';
    if (const char *data_home = std::getenv("XDG_DATA_HOME"); data_home != nullptr && *data_home != '\0') {
      add(data_home);
    } else if (have_home) {
      add(fs::path {home} / ".local" / "share");
    }
    const char *xdg_data_dirs = std::getenv("XDG_DATA_DIRS");
    const bool default_dirs = xdg_data_dirs == nullptr || *xdg_data_dirs == '\0';
    for (const auto &dir : split(default_dirs ? "/usr/local/share:/usr/share" : xdg_data_dirs, ':')) {
      add(dir);
    }
    if (default_dirs) {
      // Flatpak lists its export directories in XDG_DATA_DIRS when that is set
      // by the session; without it they would otherwise be invisible.
      if (have_home) {
        add(fs::path {home} / ".local" / "share" / "flatpak" / "exports" / "share");
      }
      add("/var/lib/flatpak/exports/share");
    }
    return dirs;
  }

  std::vector<app_t> installed_apps(const std::vector<fs::path> &dirs, std::string_view current_desktop) {
    const auto desktops = split(current_desktop, ':');
    std::vector<app_t> apps;
    std::set<std::string> ids;
    std::set<std::string> names;
    for (const auto &dir : dirs) {
      const fs::path applications = dir / "applications";
      for (const auto &file : desktop_files(applications)) {
        // A desktop file ID hides the same ID in later directories even when
        // the earlier entry is skipped below (that is how users hide an app).
        auto id = file.lexically_relative(applications).generic_string();
        std::replace(id.begin(), id.end(), '/', '-');
        if (!ids.insert(id).second) {
          continue;
        }
        id.erase(id.size() - ".desktop"sv.size());

        const auto entry = read_entry(file);
        if (value(entry, "Type") != "Application"sv || value(entry, "NoDisplay") == "true"sv || value(entry, "Hidden") == "true"sv || value(entry, "Terminal") == "true"sv) {
          continue;
        }
        auto name = value(entry, "Name");
        auto command = exec_command(value(entry, "Exec"));
        if (name.empty() || command.empty()) {
          continue;
        }
        if (const auto try_exec = value(entry, "TryExec"); !try_exec.empty() && !program_exists(try_exec)) {
          continue;
        }
        if (!shown_in_desktop(entry, desktops) || !names.insert(name).second) {
          continue;
        }
        const bool flatpak = !value(entry, "X-Flatpak").empty() || dir.generic_string().find("/flatpak/exports/"sv) != std::string::npos;
        apps.push_back({std::move(id), std::move(name), std::move(command), find_icon(value(entry, "Icon"), dirs), flatpak});
      }
    }
    std::sort(apps.begin(), apps.end(), [](const app_t &a, const app_t &b) {
      const auto a_name = lowercase(a.name);
      const auto b_name = lowercase(b.name);
      return a_name != b_name ? a_name < b_name : a.name < b.name;
    });
    return apps;
  }

  std::vector<app_t> installed_apps() {
    const char *current_desktop = std::getenv("XDG_CURRENT_DESKTOP");
    return installed_apps(data_dirs(), current_desktop != nullptr ? current_desktop : ""sv);
  }

}  // namespace prism::desktop
