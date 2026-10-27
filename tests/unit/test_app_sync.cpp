/**
 * @file tests/unit/test_app_sync.cpp
 * @brief Test src/app_sync.* functions.
 */
// test imports
#include "../tests_common.h"

// standard imports
#include <string>
#include <vector>

// local imports
#include <src/app_sync.h>

namespace {
  using prism::sync::category_e;
  using prism::sync::entry_t;

  /**
   * @brief Build a desktop application.
   *
   * @param id Desktop file ID.
   * @param name Display name.
   * @param flatpak Whether Flatpak exports the application.
   * @param command Launch command.
   * @return The application.
   */
  prism::desktop::app_t desktop_app(const std::string &id, const std::string &name, bool flatpak = false, const std::string &command = "run") {
    return {id, name, command, {}, flatpak};
  }

  /**
   * @brief List the IDs of entries.
   *
   * @param entries Entries to list.
   * @return IDs in order.
   */
  std::vector<std::string> ids(const std::vector<entry_t> &entries) {
    std::vector<std::string> result;
    for (const auto &entry : entries) {
      result.push_back(entry.id);
    }
    return result;
  }
}  // namespace

TEST(AppSyncCategory, NamesAndModes) {
  using config::app_sync_mode_e;
  const config::app_sync_t settings {app_sync_mode_e::all, app_sync_mode_e::selected, app_sync_mode_e::none, {}};

  EXPECT_EQ(prism::sync::category_name(category_e::steam), "steam");
  EXPECT_EQ(prism::sync::category_name(category_e::flatpak), "flatpak");
  EXPECT_EQ(prism::sync::category_name(category_e::system), "system");

  EXPECT_EQ(prism::sync::mode_of(category_e::steam, settings), app_sync_mode_e::all);
  EXPECT_EQ(prism::sync::mode_of(category_e::flatpak, settings), app_sync_mode_e::selected);
  EXPECT_EQ(prism::sync::mode_of(category_e::system, settings), app_sync_mode_e::none);
}

TEST(AppSyncEntries, CombinesSteamGamesAndDesktopApps) {
  const std::vector<prism::steam::game_t> games {
    {730, "Counter-Strike 2", "/art/730.jpg"},
    {440, "Team Fortress 2", {}},
  };
  std::vector<prism::desktop::app_t> apps {
    desktop_app("firefox", "Firefox", true, "flatpak run org.mozilla.firefox"),
    desktop_app("org.kde.dolphin", "Dolphin"),
    desktop_app("org.gnome.Calculator", "Calculator", true),
  };
  apps[1].icon = "/icons/dolphin.png";

  const auto entries = prism::sync::make_entries(games, apps);
  EXPECT_EQ(
    ids(entries),
    (std::vector<std::string> {"steam:730", "steam:440", "flatpak:firefox", "flatpak:org.gnome.Calculator", "system:org.kde.dolphin"})
  );

  EXPECT_EQ(entries[0].category, category_e::steam);
  EXPECT_EQ(entries[0].name, "Counter-Strike 2");
  EXPECT_EQ(entries[0].command, "steam steam://rungameid/730");
  EXPECT_EQ(entries[0].box_art, "/art/730.jpg");
  EXPECT_EQ(entries[0].steam_appid, 730u);

  EXPECT_EQ(entries[2].category, category_e::flatpak);
  EXPECT_EQ(entries[2].command, "flatpak run org.mozilla.firefox");
  EXPECT_EQ(entries[2].steam_appid, 0u);

  EXPECT_EQ(entries[4].category, category_e::system);
  EXPECT_EQ(entries[4].icon, "/icons/dolphin.png");
}

TEST(AppSyncEntries, LeavesOutSteamLaunchers) {
  const std::vector<prism::desktop::app_t> apps {
    desktop_app("steam", "Steam"),
    desktop_app("steamlink", "STEAM Link"),
    desktop_app("portal2", "Portal 2", false, "steam steam://rungameid/620"),
    desktop_app("keep", "Keep"),
  };

  EXPECT_EQ(ids(prism::sync::make_entries({}, apps)), (std::vector<std::string> {"system:keep"}));
}

TEST(AppSyncEntries, LeavesOutPrismsOwnLaunchers) {
  const std::string prism_id = PROJECT_FQDN;
  const std::vector<prism::desktop::app_t> apps {
    desktop_app(prism_id, "Prism"),
    desktop_app(prism_id + ".kwin", "Prism KWin"),
    desktop_app(prism_id + ".terminal", "Prism Terminal"),
    desktop_app("not-" + prism_id, "Keep"),
  };

  EXPECT_EQ(ids(prism::sync::make_entries({}, apps)), (std::vector<std::string> {"system:not-" + prism_id}));
}

TEST(AppSyncEntries, LeavesOutIDsThatCannotBeConfigured) {
  const std::vector<prism::desktop::app_t> apps {
    desktop_app("has space", "Has Space"),
    desktop_app("comma,id", "Comma"),
    desktop_app("fine-id_1.0", "Fine"),
  };

  EXPECT_EQ(ids(prism::sync::make_entries({}, apps)), (std::vector<std::string> {"system:fine-id_1.0"}));
}

TEST(AppSyncEntries, EmptyWhenNothingIsInstalled) {
  EXPECT_TRUE(prism::sync::make_entries({}, {}).empty());
}

class AppSyncVisibilityTest: public BaseTest {
protected:
  void SetUp() override {
    BaseTest::SetUp();
    entries = prism::sync::make_entries(
      {{730, "Counter-Strike 2", {}}, {440, "Team Fortress 2", {}}},
      {desktop_app("firefox", "Firefox", true), desktop_app("dolphin", "Dolphin"), desktop_app("kate", "Kate")}
    );
  }

  std::vector<entry_t> entries;  ///< Two Steam games, one Flatpak app and two system apps.
};

TEST_F(AppSyncVisibilityTest, AllListsEveryEntryOfTheCategory) {
  using config::app_sync_mode_e;
  const config::app_sync_t settings {app_sync_mode_e::all, app_sync_mode_e::all, app_sync_mode_e::all, {}};

  EXPECT_EQ(prism::sync::visible(entries, settings).size(), entries.size());
}

TEST_F(AppSyncVisibilityTest, NoneHidesTheCategory) {
  using config::app_sync_mode_e;
  const config::app_sync_t settings {app_sync_mode_e::none, app_sync_mode_e::all, app_sync_mode_e::none, {"steam:730", "system:kate"}};

  EXPECT_EQ(ids(prism::sync::visible(entries, settings)), (std::vector<std::string> {"flatpak:firefox"}));
}

TEST_F(AppSyncVisibilityTest, SelectedListsOnlyNamedEntries) {
  using config::app_sync_mode_e;
  const config::app_sync_t settings {
    app_sync_mode_e::selected,
    app_sync_mode_e::selected,
    app_sync_mode_e::selected,
    {"system:kate", "steam:440", "flatpak:not-installed"},
  };

  EXPECT_EQ(ids(prism::sync::visible(entries, settings)), (std::vector<std::string> {"steam:440", "system:kate"}));
}

TEST_F(AppSyncVisibilityTest, SelectedIDsOnlyApplyToTheirOwnCategory) {
  using config::app_sync_mode_e;
  const config::app_sync_t settings {app_sync_mode_e::none, app_sync_mode_e::none, app_sync_mode_e::selected, {"steam:730", "flatpak:firefox", "system:dolphin"}};

  EXPECT_EQ(ids(prism::sync::visible(entries, settings)), (std::vector<std::string> {"system:dolphin"}));
}
