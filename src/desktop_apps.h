/**
 * @file desktop_apps.h
 * @brief Enumerate installed freedesktop.org GUI applications so Prism can sync them into the app list.
 */
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace prism::desktop {

  /**
   * @brief One launchable GUI application described by a `.desktop` entry.
   */
  struct app_t {
    std::string id;  ///< Desktop file ID without the `.desktop` suffix (`org.kde.dolphin`; sub-directories become dashes).
    std::string name;  ///< Display name from the entry's `Name` key.
    std::string command;  ///< Shell-safe command line with the entry's field codes removed.
    std::filesystem::path icon;  ///< PNG icon (may be empty when none was found).
    bool flatpak = false;  ///< Whether the entry is exported by Flatpak (`X-Flatpak` key or a `flatpak/exports` data directory).
  };

  /**
   * @brief Convert a desktop entry `Exec` value into a command line a shell can run.
   *
   * Splits the value with the desktop entry quoting rules, drops the field codes
   * (`%f`, `%U`, ...) and Flatpak's `@@` file markers because Prism launches the
   * application without any files, and shell-quotes every remaining argument.
   *
   * @param exec Raw `Exec` value.
   * @return The command line, or an empty string when nothing is left to run.
   */
  std::string exec_command(std::string_view exec);

  /**
   * @brief Locate the data directories that may hold `applications` and `icons`.
   *
   * Uses `XDG_DATA_HOME` and `XDG_DATA_DIRS` (with their specification defaults).
   * When `XDG_DATA_DIRS` is unset, the Flatpak export directories are appended
   * because nothing else would list them. Earlier directories take precedence.
   *
   * @return Unique data directories, highest precedence first.
   */
  std::vector<std::filesystem::path> data_dirs();

  /**
   * @brief Enumerate the GUI applications under explicit data directories.
   *
   * Reads every `applications/<id>.desktop` entry. An entry is skipped when it is
   * not an `Application`, is hidden (`NoDisplay`, `Hidden`), needs a terminal,
   * lacks a name or command, names a missing `TryExec` program, or is excluded for
   * the current desktop by `OnlyShowIn`/`NotShowIn`. A desktop file ID found in
   * an earlier directory hides the same ID in later ones, and only the first
   * application with a given name is kept. Entries are marked as Flatpak apps
   * when they carry an `X-Flatpak` key or come from a `flatpak/exports` directory.
   *
   * @param dirs Data directories, highest precedence first.
   * @param current_desktop Value of `XDG_CURRENT_DESKTOP` (colon-separated names).
   * @return Applications sorted by name; empty when none are found.
   */
  std::vector<app_t> installed_apps(const std::vector<std::filesystem::path> &dirs, std::string_view current_desktop);

  /**
   * @brief Enumerate the GUI applications installed for the current user.
   *
   * @return Applications sorted by name; empty when none are found.
   */
  std::vector<app_t> installed_apps();

}  // namespace prism::desktop
