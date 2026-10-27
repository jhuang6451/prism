/**
 * @file app_sync.cpp
 * @brief Discover installed games and GUI applications and filter them by the app sync settings.
 */
#include "app_sync.h"

#include <algorithm>
#include <cctype>
#include <iterator>

using namespace std::literals;

namespace {

  /**
   * @brief Check whether a desktop application is Steam itself or a Steam game shortcut.
   *
   * These start a Steam session, which the Steam games already provide.
   *
   * @param app Desktop application.
   * @return True for names containing "steam" and for `steam steam://rungameid/` launches.
   */
  bool is_steam_app(const prism::desktop::app_t &app) {
    std::string name = app.name;
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    return name.find("steam"sv) != std::string::npos || app.command.starts_with("steam steam://rungameid/"sv);
  }

  /**
   * @brief Check whether a desktop application is one of Prism's own launchers.
   *
   * Streaming Prism's launcher from inside a stream would be pointless.
   *
   * @param app Desktop application.
   * @return True for desktop file IDs that start with the Prism application ID.
   */
  bool is_prism_app(const prism::desktop::app_t &app) {
    return app.id.starts_with(std::string_view {PROJECT_FQDN});
  }

}  // namespace

namespace prism::sync {

  std::string_view category_name(category_e category) {
    if (category == category_e::steam) {
      return "steam"sv;
    }
    return category == category_e::flatpak ? "flatpak"sv : "system"sv;
  }

  config::app_sync_mode_e mode_of(category_e category, const config::app_sync_t &settings) {
    if (category == category_e::steam) {
      return settings.steam;
    }
    return category == category_e::flatpak ? settings.flatpak : settings.system;
  }

  std::vector<entry_t> make_entries(const std::vector<steam::game_t> &games, const std::vector<desktop::app_t> &apps) {
    std::vector<entry_t> entries;
    for (const auto &game : games) {
      entries.push_back({
        category_e::steam,
        "steam:"s + std::to_string(game.appid),
        game.name,
        "steam steam://rungameid/"s + std::to_string(game.appid),
        {},
        game.box_art,
        game.appid,
      });
    }
    for (const auto category : {category_e::flatpak, category_e::system}) {
      for (const auto &app : apps) {
        if ((app.flatpak ? category_e::flatpak : category_e::system) != category || is_steam_app(app) || is_prism_app(app)) {
          continue;
        }
        auto id = std::string {category_name(category)} + ':' + app.id;
        if (config::valid_app_id(id)) {
          entries.push_back({category, std::move(id), app.name, app.command, app.icon, {}, 0});
        }
      }
    }
    return entries;
  }

  std::vector<entry_t> scan() {
    return make_entries(steam::installed_games(), desktop::installed_apps());
  }

  bool is_visible(const entry_t &entry, const config::app_sync_t &settings) {
    const auto mode = mode_of(entry.category, settings);
    if (mode == config::app_sync_mode_e::selected) {
      return std::find(settings.selected.begin(), settings.selected.end(), entry.id) != settings.selected.end();
    }
    return mode == config::app_sync_mode_e::all;
  }

  std::vector<entry_t> visible(const std::vector<entry_t> &entries, const config::app_sync_t &settings) {
    std::vector<entry_t> result;
    std::copy_if(entries.begin(), entries.end(), std::back_inserter(result), [&settings](const entry_t &entry) {
      return is_visible(entry, settings);
    });
    return result;
  }

}  // namespace prism::sync
