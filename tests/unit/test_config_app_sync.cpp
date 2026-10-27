/**
 * @file tests/unit/test_config_app_sync.cpp
 * @brief Test the app sync settings and the configuration file editing in src/config.*.
 */
// test imports
#include "../tests_common.h"

// standard imports
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// local imports
#include <src/config.h>

namespace {
  using config::app_sync_mode_e;

  /**
   * @brief Parse app sync options from configuration text.
   *
   * @param text Configuration file text.
   * @param settings Settings to update.
   * @return Entries that were not consumed.
   */
  std::unordered_map<std::string, std::string> parse(const std::string &text, config::app_sync_t &settings) {
    auto vars = config::parse_config(text);
    config::parse_app_sync(vars, settings);
    return vars;
  }
}  // namespace

TEST(AppSyncMode, NamesRoundTrip) {
  for (const auto mode : {app_sync_mode_e::all, app_sync_mode_e::selected, app_sync_mode_e::none}) {
    EXPECT_EQ(config::parse_app_sync_mode(config::app_sync_mode_name(mode)), mode);
  }
  EXPECT_EQ(config::app_sync_mode_name(app_sync_mode_e::all), "all");
  EXPECT_EQ(config::app_sync_mode_name(app_sync_mode_e::selected), "selected");
  EXPECT_EQ(config::app_sync_mode_name(app_sync_mode_e::none), "none");
}

TEST(AppSyncMode, ParsingIgnoresCaseBlanksAndQuotes) {
  EXPECT_EQ(config::parse_app_sync_mode("ALL"), app_sync_mode_e::all);
  EXPECT_EQ(config::parse_app_sync_mode("  Selected \t"), app_sync_mode_e::selected);
  EXPECT_EQ(config::parse_app_sync_mode("\"none\""), app_sync_mode_e::none);
  EXPECT_EQ(config::parse_app_sync_mode("'all'"), app_sync_mode_e::all);
  EXPECT_FALSE(config::parse_app_sync_mode("").has_value());
  EXPECT_FALSE(config::parse_app_sync_mode("some").has_value());
  EXPECT_FALSE(config::parse_app_sync_mode("enabled").has_value());
}

TEST(AppSyncId, AcceptsConfigurableIDs) {
  EXPECT_TRUE(config::valid_app_id("steam:730"));
  EXPECT_TRUE(config::valid_app_id("steam:4294967295"));
  EXPECT_TRUE(config::valid_app_id("flatpak:org.mozilla.firefox"));
  EXPECT_TRUE(config::valid_app_id("system:org.kde.dolphin"));
  EXPECT_TRUE(config::valid_app_id("system:Some-App_2+x@host"));
}

TEST(AppSyncId, RejectsEverythingElse) {
  EXPECT_FALSE(config::valid_app_id(""));
  EXPECT_FALSE(config::valid_app_id("steam"));
  EXPECT_FALSE(config::valid_app_id("steam:"));
  EXPECT_FALSE(config::valid_app_id("steam:abc"));
  EXPECT_FALSE(config::valid_app_id("steam:12345678901"));
  EXPECT_FALSE(config::valid_app_id("steam:7 30"));
  EXPECT_FALSE(config::valid_app_id("unknown:app"));
  EXPECT_FALSE(config::valid_app_id(":app"));
  EXPECT_FALSE(config::valid_app_id("flatpak:has space"));
  EXPECT_FALSE(config::valid_app_id("system:a,b"));
  EXPECT_FALSE(config::valid_app_id("system:a#b"));
  EXPECT_FALSE(config::valid_app_id("system:[x]"));
  EXPECT_FALSE(config::valid_app_id("system:\"x\""));
  EXPECT_FALSE(config::valid_app_id("system:caf\xC3\xA9"));
}

TEST(AppSyncParse, ReadsEveryOption) {
  config::app_sync_t settings {app_sync_mode_e::all, app_sync_mode_e::all, app_sync_mode_e::all, {}};
  const auto left = parse(
    "app_sync_steam = none\n"
    "app_sync_flatpak = all\n"
    "app_sync_system = selected\n"
    "app_sync_selected = [steam:730, flatpak:org.mozilla.firefox,system:kate]\n"
    "port = 1234\n",
    settings
  );

  EXPECT_EQ(settings.steam, app_sync_mode_e::none);
  EXPECT_EQ(settings.flatpak, app_sync_mode_e::all);
  EXPECT_EQ(settings.system, app_sync_mode_e::selected);
  EXPECT_EQ(settings.selected, (std::vector<std::string> {"steam:730", "flatpak:org.mozilla.firefox", "system:kate"}));
  EXPECT_EQ(left.size(), 1);
  EXPECT_TRUE(left.contains("port"));
}

TEST(AppSyncParse, AbsentOptionsKeepTheCurrentValues) {
  config::app_sync_t settings {app_sync_mode_e::none, app_sync_mode_e::selected, app_sync_mode_e::all, {"system:kate"}};
  parse("# nothing about app sync\nport = 1\n", settings);

  EXPECT_EQ(settings.steam, app_sync_mode_e::none);
  EXPECT_EQ(settings.flatpak, app_sync_mode_e::selected);
  EXPECT_EQ(settings.system, app_sync_mode_e::all);
  EXPECT_EQ(settings.selected, (std::vector<std::string> {"system:kate"}));
}

TEST(AppSyncParse, UnknownModesAreIgnored) {
  config::app_sync_t settings {app_sync_mode_e::all, app_sync_mode_e::selected, app_sync_mode_e::none, {}};
  parse("app_sync_steam = sometimes\napp_sync_flatpak = enabled\napp_sync_system = ALL\n", settings);

  EXPECT_EQ(settings.steam, app_sync_mode_e::all);
  EXPECT_EQ(settings.flatpak, app_sync_mode_e::selected);
  EXPECT_EQ(settings.system, app_sync_mode_e::all);
}

TEST(AppSyncParse, SelectedAcceptsMultiLineListsCommentsAndQuotes) {
  config::app_sync_t settings {app_sync_mode_e::all, app_sync_mode_e::all, app_sync_mode_e::all, {}};
  parse(
    "app_sync_selected = [  # my picks\n"
    "  steam:730,\n"
    "  \"flatpak:org.mozilla.firefox\",   # browser\n"
    "  'system:kate'\n"
    "]\n",
    settings
  );

  EXPECT_EQ(settings.selected, (std::vector<std::string> {"steam:730", "flatpak:org.mozilla.firefox", "system:kate"}));
}

TEST(AppSyncParse, SelectedDropsInvalidAndDuplicateEntries) {
  config::app_sync_t settings {app_sync_mode_e::all, app_sync_mode_e::all, app_sync_mode_e::all, {}};
  parse("app_sync_selected = [steam:730, bogus, system:kate, steam:730, system:a b, flatpak:]\n", settings);

  EXPECT_EQ(settings.selected, (std::vector<std::string> {"steam:730", "system:kate"}));
}

TEST(AppSyncParse, SelectedAcceptsPlainCommaSeparatedValues) {
  config::app_sync_t settings {app_sync_mode_e::all, app_sync_mode_e::all, app_sync_mode_e::all, {}};
  parse("app_sync_selected = steam:730, system:kate\n", settings);

  EXPECT_EQ(settings.selected, (std::vector<std::string> {"steam:730", "system:kate"}));
}

TEST(AppSyncParse, EmptyListClearsTheSelection) {
  config::app_sync_t settings {app_sync_mode_e::all, app_sync_mode_e::all, app_sync_mode_e::all, {"steam:730"}};
  parse("app_sync_selected = []\n", settings);

  EXPECT_TRUE(settings.selected.empty());
}

TEST(AppSyncOptions, RenderTheFileFormat) {
  const auto options = config::app_sync_options({app_sync_mode_e::all, app_sync_mode_e::selected, app_sync_mode_e::none, {"steam:730", "system:kate"}});

  ASSERT_EQ(options.size(), 4);
  EXPECT_EQ(options[0], (std::pair<std::string, std::string> {"app_sync_steam", "all"}));
  EXPECT_EQ(options[1], (std::pair<std::string, std::string> {"app_sync_flatpak", "selected"}));
  EXPECT_EQ(options[2], (std::pair<std::string, std::string> {"app_sync_system", "none"}));
  EXPECT_EQ(options[3], (std::pair<std::string, std::string> {"app_sync_selected", "[\n  steam:730,\n  system:kate\n]"}));
}

TEST(AppSyncOptions, EmptySelectionIsAnEmptyList) {
  const auto options = config::app_sync_options({app_sync_mode_e::all, app_sync_mode_e::all, app_sync_mode_e::all, {}});

  EXPECT_EQ(options[3].second, "[]");
}

TEST(AppSyncOptions, RoundTripThroughTheConfigurationFile) {
  const config::app_sync_t original {
    app_sync_mode_e::none,
    app_sync_mode_e::selected,
    app_sync_mode_e::all,
    {"steam:730", "flatpak:org.mozilla.firefox", "system:org.kde.dolphin"},
  };

  const auto text = config::set_options("# my settings\nport = 47989\n", config::app_sync_options(original));
  config::app_sync_t restored {app_sync_mode_e::all, app_sync_mode_e::all, app_sync_mode_e::all, {"steam:1"}};
  const auto left = parse(text, restored);

  EXPECT_EQ(restored.steam, original.steam);
  EXPECT_EQ(restored.flatpak, original.flatpak);
  EXPECT_EQ(restored.system, original.system);
  EXPECT_EQ(restored.selected, original.selected);
  EXPECT_EQ(left.size(), 1);
  EXPECT_EQ(left.at("port"), "47989");
}

TEST(AppSyncOptions, EmptySelectionRoundTrips) {
  const config::app_sync_t original {app_sync_mode_e::all, app_sync_mode_e::all, app_sync_mode_e::all, {}};
  config::app_sync_t restored {app_sync_mode_e::all, app_sync_mode_e::all, app_sync_mode_e::all, {"steam:1"}};
  parse(config::set_options("", config::app_sync_options(original)), restored);

  EXPECT_TRUE(restored.selected.empty());
}

TEST(SetOptions, AppendsMissingOptions) {
  EXPECT_EQ(config::set_options("", {{"a", "1"}, {"b", "2"}}), "a = 1\nb = 2\n");
  EXPECT_EQ(config::set_options("port = 1\n", {{"a", "1"}}), "port = 1\na = 1\n");
}

TEST(SetOptions, TerminatesAnUnterminatedLastLineBeforeAppending) {
  EXPECT_EQ(config::set_options("port = 1", {{"a", "1"}}), "port = 1\na = 1\n");
}

TEST(SetOptions, ReplacesInPlaceAndKeepsEverythingElse) {
  const std::string content =
    "# Prism settings\n"
    "port = 47989\n"
    "\n"
    "   a = old   # trailing comment\n"
    "# a comment about b\n"
    "b = 2\n"
    "app_sync_steam_extra = keep\n";

  EXPECT_EQ(
    config::set_options(content, {{"a", "new"}}),
    "# Prism settings\n"
    "port = 47989\n"
    "\n"
    "   a = new\n"
    "# a comment about b\n"
    "b = 2\n"
    "app_sync_steam_extra = keep\n"
  );
}

TEST(SetOptions, ReplacesAWholeMultiLineList) {
  const std::string content =
    "before = 1\n"
    "list = [\n"
    "  x,  # note\n"
    "  y\n"
    "]\n"
    "after = 2\n";

  EXPECT_EQ(
    config::set_options(content, {{"list", "[z]"}}),
    "before = 1\n"
    "list = [z]\n"
    "after = 2\n"
  );
}

TEST(SetOptions, RemovesDuplicatesAndOptionsWithEmptyValues) {
  const std::string content =
    "a = 1\n"
    "b = 2\n"
    "a = 3\n"
    "c = 4\n";

  EXPECT_EQ(config::set_options(content, {{"a", "9"}, {"b", ""}, {"missing", ""}}), "a = 9\nc = 4\n");
}

TEST(SetOptions, KeepsWindowsLineEndings) {
  EXPECT_EQ(
    config::set_options("a = 1\r\nb = 2\r\nc = 3\r\n", {{"b", "9"}, {"a", ""}}),
    "b = 9\r\nc = 3\r\n"
  );
}

TEST(SetOptions, ChangesNothingWhenThereIsNothingToSet) {
  const std::string content = "# only a comment\n\nport = 1\nbroken line\n";

  EXPECT_EQ(config::set_options(content, {}), content);
  EXPECT_EQ(config::set_options(content, {{"other", ""}}), content);
}

class AppSyncSettingsTest: public BaseTest {
protected:
  void SetUp() override {
    BaseTest::SetUp();
    saved = config::app_sync_snapshot();
    saved_config_file = config::prism.config_file;
    config_path = std::filesystem::temp_directory_path() / "prism_app_sync_settings_test.conf";  // NOSONAR(cpp:S5443): safe for tests
    std::filesystem::remove_all(config_path);
    config::prism.config_file = config_path.string();
  }

  void TearDown() override {
    config::prism.config_file = saved_config_file;
    std::filesystem::remove_all(config_path);
    config::set_app_sync(saved);
    BaseTest::TearDown();
  }

  config::app_sync_t saved;  ///< Settings to restore after the test.
  std::string saved_config_file;  ///< Configuration file path to restore after the test.
  std::filesystem::path config_path;  ///< Configuration file used by the test.
};

TEST_F(AppSyncSettingsTest, ReloadReadsTheConfigurationFile) {
  std::ofstream(config_path) << "app_sync_steam = none\napp_sync_flatpak = all\napp_sync_system = selected\napp_sync_selected = [system:kate]\n";
  config::set_app_sync({app_sync_mode_e::all, app_sync_mode_e::all, app_sync_mode_e::all, {}});

  EXPECT_TRUE(config::reload_app_sync());
  const auto snapshot = config::app_sync_snapshot();
  EXPECT_EQ(snapshot.steam, app_sync_mode_e::none);
  EXPECT_EQ(snapshot.flatpak, app_sync_mode_e::all);
  EXPECT_EQ(snapshot.system, app_sync_mode_e::selected);
  EXPECT_EQ(snapshot.selected, (std::vector<std::string> {"system:kate"}));
}

TEST_F(AppSyncSettingsTest, ReloadResetsOptionsMissingFromTheFileToTheirDefaults) {
  std::ofstream(config_path) << "# nothing about app sync\nport = 1\n";
  config::set_app_sync({app_sync_mode_e::none, app_sync_mode_e::none, app_sync_mode_e::all, {"steam:730"}});

  EXPECT_TRUE(config::reload_app_sync());
  const auto snapshot = config::app_sync_snapshot();
  EXPECT_EQ(snapshot.steam, app_sync_mode_e::all);
  EXPECT_EQ(snapshot.flatpak, app_sync_mode_e::selected);
  EXPECT_EQ(snapshot.system, app_sync_mode_e::selected);
  EXPECT_TRUE(snapshot.selected.empty());
}

TEST_F(AppSyncSettingsTest, ReloadOfAnEmptyFileGivesTheDefaults) {
  std::ofstream {config_path};
  config::set_app_sync({app_sync_mode_e::none, app_sync_mode_e::none, app_sync_mode_e::none, {}});

  EXPECT_TRUE(config::reload_app_sync());
  EXPECT_EQ(config::app_sync_snapshot().steam, app_sync_mode_e::all);
}

TEST_F(AppSyncSettingsTest, ReloadKeepsTheSettingsWhenTheFileCannotBeRead) {
  config::set_app_sync({app_sync_mode_e::none, app_sync_mode_e::all, app_sync_mode_e::none, {"steam:730"}});

  EXPECT_FALSE(config::reload_app_sync());  // missing file
  std::filesystem::create_directories(config_path);
  EXPECT_FALSE(config::reload_app_sync());  // not a regular file

  const auto snapshot = config::app_sync_snapshot();
  EXPECT_EQ(snapshot.steam, app_sync_mode_e::none);
  EXPECT_EQ(snapshot.flatpak, app_sync_mode_e::all);
  EXPECT_EQ(snapshot.selected, (std::vector<std::string> {"steam:730"}));
}

TEST_F(AppSyncSettingsTest, SetAndSnapshotRoundTrip) {
  config::set_app_sync({app_sync_mode_e::none, app_sync_mode_e::all, app_sync_mode_e::selected, {"steam:730"}});
  const auto snapshot = config::app_sync_snapshot();

  EXPECT_EQ(snapshot.steam, app_sync_mode_e::none);
  EXPECT_EQ(snapshot.flatpak, app_sync_mode_e::all);
  EXPECT_EQ(snapshot.system, app_sync_mode_e::selected);
  EXPECT_EQ(snapshot.selected, (std::vector<std::string> {"steam:730"}));
}

TEST_F(AppSyncSettingsTest, SetDropsInvalidAndDuplicateIDs) {
  config::set_app_sync({app_sync_mode_e::all, app_sync_mode_e::all, app_sync_mode_e::all, {" steam:730 ", "bad", "steam:730", "", "system:kate"}});

  EXPECT_EQ(config::app_sync_snapshot().selected, (std::vector<std::string> {"steam:730", "system:kate"}));
}

TEST_F(AppSyncSettingsTest, StartsWithSteamListedAndOtherAppsHidden) {
  // The defaults documented for the configuration file: nothing but Steam games until selected.
  config::set_app_sync({app_sync_mode_e::all, app_sync_mode_e::selected, app_sync_mode_e::selected, {}});
  const auto snapshot = config::app_sync_snapshot();

  EXPECT_EQ(snapshot.steam, app_sync_mode_e::all);
  EXPECT_EQ(snapshot.flatpak, app_sync_mode_e::selected);
  EXPECT_EQ(snapshot.system, app_sync_mode_e::selected);
  EXPECT_TRUE(snapshot.selected.empty());
}
