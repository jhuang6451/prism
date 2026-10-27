/**
 * @file app_sync.h
 * @brief Discover installed games and GUI applications and filter them by the app sync settings.
 */
#pragma once

#include "config.h"
#include "desktop_apps.h"
#include "steam_games.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace prism::sync {

  /**
   * @brief Group an application is listed under in the Web UI and selected by in the configuration.
   */
  enum class category_e {
    steam,  ///< Installed Steam games.
    flatpak,  ///< GUI applications exported by Flatpak.
    system  ///< Other desktop GUI applications.
  };

  /**
   * @brief One discovered application that can be synced into the client application list.
   */
  struct entry_t {
    category_e category;  ///< Group of the application.
    std::string id;  ///< Stable ID used by `app_sync_selected` (`steam:730`, `flatpak:org.example.App`, `system:app`).
    std::string name;  ///< Display name shown to clients.
    std::string command;  ///< Shell command that starts the application.
    std::filesystem::path icon;  ///< PNG icon of a desktop application (may be empty).
    std::filesystem::path box_art;  ///< JPEG box art of a Steam game from Steam's library cache (may be empty).
    std::uint32_t steam_appid = 0;  ///< Steam app id of a Steam game, zero for other applications.
  };

  /**
   * @brief Get the name of a category as used in the Web UI API.
   *
   * @param category Category to describe.
   * @return `steam`, `flatpak` or `system`.
   */
  std::string_view category_name(category_e category);

  /**
   * @brief Get the sync mode configured for a category.
   *
   * @param category Category to look up.
   * @param settings Application sync settings.
   * @return The category's mode.
   */
  config::app_sync_mode_e mode_of(category_e category, const config::app_sync_t &settings);

  /**
   * @brief Combine discovered Steam games and desktop applications into sync entries.
   *
   * Desktop entries for Steam itself or for a Steam game are left out because the
   * Steam games cover them, as are Prism's own launchers and entries whose ID
   * cannot be written to the configuration file.
   *
   * @param games Installed Steam games.
   * @param apps Installed desktop applications.
   * @return Steam games first, then Flatpak applications, then the other applications.
   */
  std::vector<entry_t> make_entries(const std::vector<steam::game_t> &games, const std::vector<desktop::app_t> &apps);

  /**
   * @brief Discover everything installed for the current user.
   *
   * @return Entries in the order of make_entries(); empty when nothing is found.
   */
  std::vector<entry_t> scan();

  /**
   * @brief Check whether the settings list an application.
   *
   * @param entry Discovered application.
   * @param settings Application sync settings.
   * @return True when the category mode is `all`, or `selected` and the ID is selected.
   */
  bool is_visible(const entry_t &entry, const config::app_sync_t &settings);

  /**
   * @brief Keep the applications the settings list.
   *
   * @param entries Discovered applications.
   * @param settings Application sync settings.
   * @return The listed applications in their original order.
   */
  std::vector<entry_t> visible(const std::vector<entry_t> &entries, const config::app_sync_t &settings);

}  // namespace prism::sync
