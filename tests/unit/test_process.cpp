/**
 * @file tests/unit/test_process.cpp
 * @brief Test src/process.* functions.
 */
// test imports
#include "../tests_common.h"

// standard imports
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

// local imports
#include <src/config.h>
#include <src/process.h>

namespace fs = std::filesystem;

class SessionCleanupTest: public BaseTest {
protected:
  void SetUp() override {
    BaseTest::SetUp();
    test_dir = fs::temp_directory_path() / "prism_session_cleanup_test";
    fs::remove_all(test_dir);
    fs::create_directories(test_dir);
    setenv("PRISM_SESSION_DIR", test_dir.string().c_str(), 1);
  }

  void TearDown() override {
    unsetenv("PRISM_SESSION_DIR");
    fs::remove_all(test_dir);
    BaseTest::TearDown();
  }

  /**
   * @brief Create an executable synthetic cleanup helper.
   *
   * @param exit_code Exit status returned by the helper.
   */
  void writeCleanupHelper(const int exit_code) const {
    const fs::path helper = test_dir / "prism-session-cleanup.sh";
    std::ofstream file(helper);
    file << "#!/usr/bin/env sh\nexit " << exit_code << '\n';
    file.close();
    fs::permissions(
      helper,
      fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec
    );
  }

  fs::path test_dir;
};

TEST_F(SessionCleanupTest, SuccessfulCleanupAllowsStartup) {
  writeCleanupHelper(0);
  EXPECT_EQ(proc::reconcile_stale_capture_state(), 0);
}

TEST_F(SessionCleanupTest, FailedCleanupBlocksStartup) {
  writeCleanupHelper(23);
  EXPECT_EQ(proc::reconcile_stale_capture_state(), -1);
}

TEST_F(SessionCleanupTest, MissingCleanupHelperBlocksStartup) {
  EXPECT_EQ(proc::reconcile_stale_capture_state(), -1);
}

class ProcessPNGTest: public BaseTest {
protected:
  void SetUp() override {
    BaseTest::SetUp();
    // Create test directory
    test_dir = fs::temp_directory_path() / "prism_process_png_test";  // NOSONAR(cpp:S5443): safe for tests
    fs::create_directories(test_dir);
  }

  void TearDown() override {
    // Clean up test directory
    if (fs::exists(test_dir)) {
      fs::remove_all(test_dir);
    }
    BaseTest::TearDown();
  }

  // Helper function to create a file with specific content
  void createTestFile(const fs::path &path, const std::vector<unsigned char> &content) const {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(content.data()), content.size());
    file.close();
  }

  fs::path test_dir;
};

// Tests for check_valid_png function
TEST_F(ProcessPNGTest, CheckValidPNG_ValidSignature) {
  // Valid PNG signature
  const std::vector<unsigned char> valid_png_data = {
    0x89,
    0x50,
    0x4E,
    0x47,
    0x0D,
    0x0A,
    0x1A,
    0x0A,  // PNG signature
    // Add some dummy data to make it more realistic
    0x00,
    0x00,
    0x00,
    0x0D,
    0x49,
    0x48,
    0x44,
    0x52
  };

  const fs::path test_file = test_dir / "valid.png";
  createTestFile(test_file, valid_png_data);

  EXPECT_TRUE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_WrongSignature) {
  // Invalid PNG signature (wrong magic bytes)
  const std::vector<unsigned char> invalid_png_data = {
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00
  };

  const fs::path test_file = test_dir / "invalid.png";
  createTestFile(test_file, invalid_png_data);

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_TooShort) {
  // File too short (less than 8 bytes)
  const std::vector<unsigned char> short_data = {
    0x89,
    0x50,
    0x4E,
    0x47
  };

  const fs::path test_file = test_dir / "short.png";
  createTestFile(test_file, short_data);

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_EmptyFile) {
  // Empty file
  const std::vector<unsigned char> empty_data = {};

  const fs::path test_file = test_dir / "empty.png";
  createTestFile(test_file, empty_data);

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_NonExistentFile) {
  // File doesn't exist
  const fs::path test_file = test_dir / "nonexistent.png";

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_RealFile) {
  // Test with the actual prism.png from the project root

  // Only run this test if the file exists
  if (const fs::path prism_png = fs::path(PRISM_SOURCE_DIR) / "prism.png"; fs::exists(prism_png)) {
    EXPECT_TRUE(proc::check_valid_png(prism_png));
  } else {
    GTEST_SKIP() << "prism.png not found in project root";
  }
}

TEST_F(ProcessPNGTest, CheckValidPNG_JPEGFile) {
  // JPEG signature (not PNG)
  const std::vector<unsigned char> jpeg_data = {
    0xFF,
    0xD8,
    0xFF,
    0xE0,
    0x00,
    0x10,
    0x4A,
    0x46
  };

  const fs::path test_file = test_dir / "fake.png";
  createTestFile(test_file, jpeg_data);

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

TEST_F(ProcessPNGTest, CheckValidPNG_PartialSignature) {
  // Partial PNG signature (first 4 bytes correct, rest wrong)
  const std::vector<unsigned char> partial_png_data = {
    0x89,
    0x50,
    0x4E,
    0x47,
    0x00,
    0x00,
    0x00,
    0x00
  };

  const fs::path test_file = test_dir / "partial.png";
  createTestFile(test_file, partial_png_data);

  EXPECT_FALSE(proc::check_valid_png(test_file));
}

// Tests for validate_app_image_path function
TEST_F(ProcessPNGTest, ValidateAppImagePath_EmptyPath) {
  // Empty path should return default
  const std::string result = proc::validate_app_image_path("");
  EXPECT_EQ(result, DEFAULT_APP_IMAGE_PATH);
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_NonPNGExtension) {
  // Non-PNG extension should return default
  const std::string result = proc::validate_app_image_path("image.jpg");
  EXPECT_EQ(result, DEFAULT_APP_IMAGE_PATH);
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_CaseInsensitiveExtension) {
  // Test that .PNG (uppercase) is recognized
  // Create a valid PNG file
  const std::vector<unsigned char> valid_png_data = {
    0x89,
    0x50,
    0x4E,
    0x47,
    0x0D,
    0x0A,
    0x1A,
    0x0A,
    0x00,
    0x00,
    0x00,
    0x0D,
    0x49,
    0x48,
    0x44,
    0x52
  };

  const fs::path test_file = test_dir / "test.PNG";
  createTestFile(test_file, valid_png_data);

  const std::string result = proc::validate_app_image_path(test_file.string());
  // Should accept uppercase .PNG extension
  EXPECT_NE(result, DEFAULT_APP_IMAGE_PATH);
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_NonExistentFile) {
  // Non-existent PNG file should return default
  const std::string result = proc::validate_app_image_path("/nonexistent/path/image.png");
  EXPECT_EQ(result, DEFAULT_APP_IMAGE_PATH);
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_InvalidPNGSignature) {
  // File with .png extension but invalid signature should return default
  const std::vector<unsigned char> invalid_data = {
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00
  };

  const fs::path test_file = test_dir / "invalid.png";
  createTestFile(test_file, invalid_data);

  const std::string result = proc::validate_app_image_path(test_file.string());
  EXPECT_EQ(result, DEFAULT_APP_IMAGE_PATH);
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_ValidPNG) {
  // Valid PNG file should return the path
  const std::vector<unsigned char> valid_png_data = {
    0x89,
    0x50,
    0x4E,
    0x47,
    0x0D,
    0x0A,
    0x1A,
    0x0A,
    0x00,
    0x00,
    0x00,
    0x0D,
    0x49,
    0x48,
    0x44,
    0x52
  };

  const fs::path test_file = test_dir / "valid.png";
  createTestFile(test_file, valid_png_data);

  const std::string result = proc::validate_app_image_path(test_file.string());
  EXPECT_EQ(result, test_file.string());
}

TEST_F(ProcessPNGTest, ValidateAppImagePath_OldSteamDefault) {
  // Test the special case for old steam image path
  const std::string result = proc::validate_app_image_path("./assets/steam.png");
  EXPECT_EQ(result, PRISM_ASSETS_DIR "/steam.png");
}

// Tests for parsing the Prism per-app capture mode from apps.json
class ProcessParseTest: public BaseTest {
protected:
  void SetUp() override {
    BaseTest::SetUp();
    // App sync would otherwise append the host's real Steam library and installed applications.
    saved_app_sync = config::app_sync_snapshot();
    config::set_app_sync({config::app_sync_mode_e::none, config::app_sync_mode_e::none, config::app_sync_mode_e::none, {}});
    test_file = fs::temp_directory_path() / "prism_process_parse_test_apps.json";  // NOSONAR(cpp:S5443): safe for tests
  }

  void TearDown() override {
    config::set_app_sync(saved_app_sync);
    fs::remove(test_file);
    BaseTest::TearDown();
  }

  fs::path test_file;
  config::app_sync_t saved_app_sync;  ///< App sync settings to restore after the test.
};

TEST_F(ProcessParseTest, Parse_PrismCaptureField) {
  std::ofstream file(test_file);
  file << R"json({
    "env": {},
    "apps": [
      {"name": "Desktop"},
      {"name": "Desktop (Virtual)", "prism-capture": "virtual"},
      {"name": "SteamOS (Headless)", "prism-capture": "steamos"},
      {"name": "TV", "prism-capture": "portal:HDMI-A-1"},
      {"name": "My Game", "prism-capture": "headless"},
      {"name": "Steam Game", "prism-capture": "headless"}
    ]
  })json";
  file.close();

  auto proc_opt = proc::parse(test_file.string());
  ASSERT_TRUE(proc_opt.has_value());
  const auto &apps = proc_opt->get_apps();
  ASSERT_EQ(apps.size(), 6);
  EXPECT_TRUE(apps[0].prism_capture.empty());
  EXPECT_EQ(apps[1].prism_capture, "virtual");
  EXPECT_EQ(apps[2].prism_capture, "steamos");
  EXPECT_EQ(apps[3].prism_capture, "portal:HDMI-A-1");
  EXPECT_EQ(apps[4].prism_capture, "headless");
  EXPECT_EQ(apps[5].prism_capture, "headless");
}

// Tests for syncing installed games and desktop applications into the parsed app list
class ProcessAppSyncTest: public BaseTest {
protected:
  void SetUp() override {
    BaseTest::SetUp();
    test_dir = fs::temp_directory_path() / "prism_process_app_sync_test";  // NOSONAR(cpp:S5443): safe for tests
    fs::remove_all(test_dir);
    system_apps = test_dir / "system" / "applications";
    flatpak_apps = test_dir / "flatpak" / "exports" / "share" / "applications";
    fs::create_directories(system_apps);
    fs::create_directories(flatpak_apps);
    test_file = test_dir / "apps.json";

    // The host's Steam library and desktop entries must stay out of the test.
    saved_app_sync = config::app_sync_snapshot();
    set_env("HOME", (test_dir / "home").string().c_str());
    set_env("XDG_DATA_HOME", (test_dir / "system").string().c_str());
    set_env("XDG_DATA_DIRS", (test_dir / "flatpak" / "exports" / "share").string().c_str());
    set_env("XDG_CACHE_HOME", (test_dir / "cache").string().c_str());
    set_env("XDG_CURRENT_DESKTOP", "KDE");
    set_sync(config::app_sync_mode_e::none, config::app_sync_mode_e::none, config::app_sync_mode_e::none);

    std::ofstream(test_file) << R"json({
      "env": {},
      "apps": [
        {"name": "My App", "cmd": "mine"}
      ]
    })json";
  }

  void TearDown() override {
    config::set_app_sync(saved_app_sync);
    for (const auto &[name, old_value] : saved_env) {
      if (old_value) {
        setenv(name.c_str(), old_value->c_str(), 1);
      } else {
        unsetenv(name.c_str());
      }
    }
    fs::remove_all(test_dir);
    BaseTest::TearDown();
  }

  /**
   * @brief Set or unset an environment variable until the test ends.
   *
   * @param name Variable name.
   * @param value New value, or nullptr to unset the variable.
   */
  void set_env(const std::string &name, const char *value) {
    if (saved_env.count(name) == 0) {
      const char *old_value = std::getenv(name.c_str());
      saved_env[name] = old_value != nullptr ? std::optional<std::string> {old_value} : std::nullopt;
    }
    if (value != nullptr) {
      setenv(name.c_str(), value, 1);
    } else {
      unsetenv(name.c_str());
    }
  }

  /**
   * @brief Set the app sync modes and the selected IDs.
   *
   * @param steam Mode for Steam games.
   * @param flatpak Mode for Flatpak applications.
   * @param system Mode for the other desktop applications.
   * @param selected IDs listed by the `selected` mode.
   */
  static void set_sync(config::app_sync_mode_e steam, config::app_sync_mode_e flatpak, config::app_sync_mode_e system, std::vector<std::string> selected = {}) {
    config::set_app_sync({steam, flatpak, system, std::move(selected)});
  }

  /**
   * @brief Write a desktop entry.
   *
   * @param dir The `applications` directory to write into.
   * @param id Desktop file name.
   * @param keys Lines for the `[Desktop Entry]` group.
   */
  static void write_entry(const fs::path &dir, const std::string &id, const std::string &keys) {
    std::ofstream(dir / id) << "[Desktop Entry]\nType=Application\n"
                            << keys << '\n';
  }

  /**
   * @brief Write an installed Steam game below the test HOME.
   *
   * @param appid Steam app id.
   * @param name Game name.
   */
  void write_steam_game(const std::string &appid, const std::string &name) const {
    const fs::path root = test_dir / "home" / ".steam" / "root";
    const fs::path steamapps = root / "steamapps";
    fs::create_directories(steamapps);
    // Local box art keeps the test from downloading any from Steam's CDN.
    fs::create_directories(root / "appcache" / "librarycache" / appid);
    std::ofstream(root / "appcache" / "librarycache" / appid / "library_600x900.jpg") << "not a real jpeg";
    std::ofstream(steamapps / ("appmanifest_" + appid + ".acf")) << "\"AppState\"\n{\n\t\"appid\"\t\t\"" << appid << "\"\n\t\"name\"\t\t\"" << name << "\"\n}\n";
  }

  /**
   * @brief List the names of the parsed apps.
   *
   * @return Names in list order.
   */
  std::vector<std::string> parsed_names() const {
    auto proc_opt = proc::parse(test_file.string());
    std::vector<std::string> names;
    if (proc_opt) {
      for (const auto &app : proc_opt->get_apps()) {
        names.push_back(app.name);
      }
    }
    return names;
  }

  fs::path test_dir;  ///< Temporary root for the test.
  fs::path system_apps;  ///< `applications` directory of the system data directory.
  fs::path flatpak_apps;  ///< `applications` directory of a Flatpak export directory.
  fs::path test_file;  ///< apps.json used by the test.
  config::app_sync_t saved_app_sync;  ///< App sync settings to restore after the test.
  std::map<std::string, std::optional<std::string>> saved_env;  ///< Environment to restore.
};

TEST_F(ProcessAppSyncTest, SyncsDesktopApplicationsAsHeadlessApps) {
  const fs::path icon = test_dir / "icons" / "editor.png";
  fs::create_directories(icon.parent_path());
  std::ofstream(icon, std::ios::binary) << "\x89PNG\r\n\x1a\n";

  write_entry(system_apps, "editor.desktop", "Name=Text Editor\nExec=editor %F\nIcon=" + icon.string());
  write_entry(system_apps, "plain.desktop", "Name=Plain\nExec=plain");
  write_entry(system_apps, "collision.desktop", "Name=My App\nExec=other");
  write_entry(system_apps, "steam.desktop", "Name=Steam\nExec=steam %U");
  write_entry(system_apps, "game.desktop", "Name=Counter-Strike\nExec=steam steam://rungameid/730");
  write_entry(system_apps, "hidden.desktop", "Name=Hidden One\nExec=hidden\nNoDisplay=true");
  write_entry(flatpak_apps, "org.example.Browser.desktop", "Name=Browser\nExec=flatpak run org.example.Browser @@u %U @@");
  set_sync(config::app_sync_mode_e::none, config::app_sync_mode_e::all, config::app_sync_mode_e::all);

  auto proc_opt = proc::parse(test_file.string());
  ASSERT_TRUE(proc_opt.has_value());
  const auto &apps = proc_opt->get_apps();
  ASSERT_EQ(apps.size(), 4);

  // The user's own app wins the name collision and keeps its definition.
  EXPECT_EQ(apps[0].name, "My App");
  EXPECT_EQ(apps[0].cmd, "mine");

  // Flatpak applications come before the other system applications.
  EXPECT_EQ(apps[1].name, "Browser");
  EXPECT_EQ(apps[1].cmd, "flatpak run org.example.Browser");
  EXPECT_EQ(apps[1].prism_capture, "headless");

  EXPECT_EQ(apps[2].name, "Plain");
  EXPECT_EQ(apps[2].cmd, "plain");
  EXPECT_EQ(apps[2].prism_capture, "headless");
  EXPECT_TRUE(apps[2].image_path.empty());

  EXPECT_EQ(apps[3].name, "Text Editor");
  EXPECT_EQ(apps[3].cmd, "editor");
  EXPECT_EQ(apps[3].prism_capture, "headless");
  EXPECT_EQ(apps[3].image_path, icon.string());
  EXPECT_FALSE(apps[3].elevated);
  EXPECT_TRUE(apps[3].auto_detach);
  EXPECT_TRUE(apps[3].wait_all);

  EXPECT_FALSE(apps[2].id.empty());
  EXPECT_NE(apps[0].id, apps[1].id);
  EXPECT_NE(apps[1].id, apps[2].id);
  EXPECT_NE(apps[2].id, apps[3].id);
  EXPECT_FALSE(proc::prism_resolve_capture_mode(apps[3]).steam);
}

TEST_F(ProcessAppSyncTest, SyncsSteamGamesAsSteamApps) {
  write_steam_game("730", "Counter-Strike 2");
  write_steam_game("440", "Team Fortress 2");
  set_sync(config::app_sync_mode_e::all, config::app_sync_mode_e::none, config::app_sync_mode_e::none);

  auto proc_opt = proc::parse(test_file.string());
  ASSERT_TRUE(proc_opt.has_value());
  const auto &apps = proc_opt->get_apps();
  ASSERT_EQ(apps.size(), 3);
  EXPECT_EQ(apps[1].name, "Counter-Strike 2");
  EXPECT_EQ(apps[1].cmd, "steam steam://rungameid/730");
  EXPECT_EQ(apps[1].prism_capture, "steamos");
  EXPECT_TRUE(apps[1].auto_detach);
  EXPECT_EQ(apps[2].name, "Team Fortress 2");
  EXPECT_TRUE(proc::prism_resolve_capture_mode(apps[1]).steam);
}

TEST_F(ProcessAppSyncTest, SelectedModeListsOnlyTheChosenApps) {
  write_steam_game("730", "Counter-Strike 2");
  write_steam_game("440", "Team Fortress 2");
  write_entry(system_apps, "editor.desktop", "Name=Text Editor\nExec=editor");
  write_entry(system_apps, "plain.desktop", "Name=Plain\nExec=plain");
  write_entry(flatpak_apps, "org.example.Browser.desktop", "Name=Browser\nExec=browser");
  write_entry(flatpak_apps, "org.example.Mail.desktop", "Name=Mail\nExec=mail");
  set_sync(
    config::app_sync_mode_e::selected,
    config::app_sync_mode_e::selected,
    config::app_sync_mode_e::selected,
    {"steam:440", "flatpak:org.example.Mail", "system:editor", "system:org.example.Browser", "flatpak:uninstalled"}
  );

  EXPECT_EQ(parsed_names(), (std::vector<std::string> {"My App", "Team Fortress 2", "Mail", "Text Editor"}));
}

TEST_F(ProcessAppSyncTest, NoneModeHidesTheCategoryEvenWhenSelected) {
  write_steam_game("730", "Counter-Strike 2");
  write_entry(system_apps, "editor.desktop", "Name=Text Editor\nExec=editor");
  write_entry(flatpak_apps, "org.example.Browser.desktop", "Name=Browser\nExec=browser");
  set_sync(config::app_sync_mode_e::none, config::app_sync_mode_e::all, config::app_sync_mode_e::none, {"steam:730", "system:editor"});

  EXPECT_EQ(parsed_names(), (std::vector<std::string> {"My App", "Browser"}));
}

TEST_F(ProcessAppSyncTest, SameNamedAppsAreListedOnce) {
  write_steam_game("730", "Shared Name");
  write_entry(system_apps, "shared.desktop", "Name=Shared Name\nExec=shared");
  set_sync(config::app_sync_mode_e::all, config::app_sync_mode_e::none, config::app_sync_mode_e::all);

  const auto names = parsed_names();
  EXPECT_EQ(names, (std::vector<std::string> {"My App", "Shared Name"}));
}

// Tests for Prism capture mode resolution precedence.
class CaptureModeResolveTest: public BaseTest {
protected:
  void SetUp() override {
    BaseTest::SetUp();
    saved_default = config::stream.prism_capture_default;
  }

  void TearDown() override {
    config::stream.prism_capture_default = saved_default;
    BaseTest::TearDown();
  }

  /**
   * @brief Build a minimal app context.
   * @param name Application name.
   * @param capture Explicit prism-capture value (may be empty).
   * @param cmd Application command (may be empty).
   * @return Application context with the fields the resolver reads set.
   */
  static proc::ctx_t makeApp(const std::string &name, const std::string &capture = "", const std::string &cmd = "") {
    proc::ctx_t ctx {};
    ctx.name = name;
    ctx.prism_capture = capture;
    ctx.cmd = cmd;
    return ctx;
  }

  std::string saved_default;  ///< Saved prism_capture_default to restore after each test.
};

TEST_F(CaptureModeResolveTest, ExplicitValueWinsOverNameAndConfig) {
  config::stream.prism_capture_default = "headless";
  const auto resolved = proc::prism_resolve_capture_mode(makeApp("My Steam Game", "virtual"));
  EXPECT_EQ(resolved.mode, "virtual");
  EXPECT_FALSE(resolved.steam);
}

TEST_F(CaptureModeResolveTest, SteamosAliasEnablesSteam) {
  const auto resolved = proc::prism_resolve_capture_mode(makeApp("Anything", "steamos"));
  EXPECT_EQ(resolved.mode, "headless");
  EXPECT_TRUE(resolved.steam);
}

TEST_F(CaptureModeResolveTest, NameHeuristicsBeatConfiguredDefault) {
  config::stream.prism_capture_default = "headless";
  const auto virtual_app = proc::prism_resolve_capture_mode(makeApp("Desktop (Virtual)"));
  EXPECT_EQ(virtual_app.mode, "virtual");
  const auto steam_app = proc::prism_resolve_capture_mode(makeApp("Steam Headless"));
  EXPECT_EQ(steam_app.mode, "headless");
  EXPECT_TRUE(steam_app.steam);
}

TEST_F(CaptureModeResolveTest, ConfiguredDefaultAppliesToPlainNames) {
  config::stream.prism_capture_default = "headless";
  const auto resolved = proc::prism_resolve_capture_mode(makeApp("My Game"));
  EXPECT_EQ(resolved.mode, "headless");
  EXPECT_FALSE(resolved.steam);
}

TEST_F(CaptureModeResolveTest, PortalDefaultPassesThrough) {
  config::stream.prism_capture_default = "portal:HDMI-A-1";
  const auto resolved = proc::prism_resolve_capture_mode(makeApp("My Game"));
  EXPECT_EQ(resolved.mode, "portal:HDMI-A-1");
}

TEST_F(CaptureModeResolveTest, EmptyDefaultFallsBackToDesktopMirror) {
  config::stream.prism_capture_default = "";
  const auto resolved = proc::prism_resolve_capture_mode(makeApp("My Game"));
  EXPECT_EQ(resolved.mode, "default");
  EXPECT_FALSE(resolved.steam);
}

TEST_F(CaptureModeResolveTest, RungameidCommandForcesSteamSession) {
  // A synced game imported with a plain "headless" mode must still get Steam
  // session behavior (session Steam + launch wrapper), or the game launches
  // on the desktop and its exit never ends the stream.
  const auto imported = proc::prism_resolve_capture_mode(makeApp("9 Kings", "headless", "steam steam://rungameid/2784470"));
  EXPECT_EQ(imported.mode, "headless");
  EXPECT_TRUE(imported.steam);

  // Even with no explicit mode and a configured global default.
  config::stream.prism_capture_default = "virtual";
  const auto generated = proc::prism_resolve_capture_mode(makeApp("9 Kings", "", "steam steam://rungameid/2784470"));
  EXPECT_EQ(generated.mode, "headless");
  EXPECT_TRUE(generated.steam);
}

TEST_F(CaptureModeResolveTest, ExplicitNonHeadlessModeStillWinsForGames) {
  const auto resolved = proc::prism_resolve_capture_mode(makeApp("9 Kings", "virtual", "steam steam://rungameid/2784470"));
  EXPECT_EQ(resolved.mode, "virtual");
  EXPECT_FALSE(resolved.steam);
}

/**
 * @brief Tests validation of atomically published headless session state.
 */
class HeadlessStateTest: public BaseTest {
protected:
  void SetUp() override {
    BaseTest::SetUp();
    state_path = fs::temp_directory_path() / "prism_headless_state_test";
  }

  void TearDown() override {
    fs::remove(state_path);
    BaseTest::TearDown();
  }

  /**
   * @brief Write headless state content for one parser test.
   *
   * @param content Complete state file content.
   */
  void writeState(const std::string &content) const {
    std::ofstream state(state_path);
    state << content;
  }

  /**
   * @brief Return a complete valid version-four state.
   *
   * @param session_id Session identifier written into ownership fields.
   * @return Serialized state content.
   */
  static std::string validState(const std::string &session_id = "42") {
    return "version=4\n"
           "session_id=" +
           session_id +
           "\n"
           "backend=systemd\n"
           "unit=prism-headless-session.service\n"
           "input_unit=prism-input-bridge.service\n"
           "steam_unit=prism-headless-steam.service\n"
           "app_unit=prism-headless-app-" +
           session_id +
           ".scope\n"
           "steam=1\n"
           "wayland_display=wayland-0\n"
           "output_name=HEADLESS-1\n"
           "x_display=:9\n"
           "width=2560\n"
           "height=1440\n"
           "framerate=120\n"
           "physical_sink=speakers\n"
           "capture_sink_module=\n"
           "session_sink_module=123\n"
           "loop_module=456\n";
  }

  fs::path state_path;  ///< Temporary state file used by the test.
};

TEST_F(HeadlessStateTest, AcceptsCompleteMatchingState) {
  writeState(validState());

  const auto state = proc::prism_read_headless_state(state_path, "42");
  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(state->version, 4);
  EXPECT_EQ(state->session_id, "42");
  EXPECT_TRUE(state->steam);
  EXPECT_EQ(state->steam_unit, "prism-headless-steam.service");
  EXPECT_EQ(state->wayland_display, "wayland-0");
  EXPECT_EQ(state->output_name, "HEADLESS-1");
  EXPECT_EQ(state->x_display, ":9");
  EXPECT_EQ(state->width, 2560);
  EXPECT_EQ(state->session_sink_module, "123");
}

TEST_F(HeadlessStateTest, RejectsAnotherLaunchSession) {
  writeState(validState("41"));
  EXPECT_FALSE(proc::prism_read_headless_state(state_path, "42").has_value());
}

TEST_F(HeadlessStateTest, RejectsLegacyOrIncompleteState) {
  writeState("version=3\nsteam=1\nwayland_display=gamescope-0\nx_display=:2\n");
  EXPECT_FALSE(proc::prism_read_headless_state(state_path, "42").has_value());
}

TEST_F(HeadlessStateTest, RejectsGuessedOrMalformedDisplays) {
  auto malformed = validState();
  malformed.replace(malformed.find("wayland-0"), std::string("wayland-0").size(), "wayland-prism");
  malformed.replace(malformed.find(":9"), 2, "desktop");
  writeState(malformed);
  EXPECT_FALSE(proc::prism_read_headless_state(state_path, "42").has_value());
}

TEST_F(HeadlessStateTest, RejectsDuplicateKeys) {
  writeState(validState() + "session_id=43\n");
  EXPECT_FALSE(proc::prism_read_headless_state(state_path, "42").has_value());
}

TEST_F(HeadlessStateTest, RejectsUnknownOrOutOfRangeFields) {
  auto oversized = validState();
  oversized.replace(oversized.find("width=2560"), std::string("width=2560").size(), "width=16385");
  writeState(oversized);
  EXPECT_FALSE(proc::prism_read_headless_state(state_path, "42").has_value());

  writeState(validState() + "unknown=value\n");
  EXPECT_FALSE(proc::prism_read_headless_state(state_path, "42").has_value());

  auto oversized_display = validState();
  oversized_display.replace(oversized_display.find("x_display=:9"), std::string("x_display=:9").size(), "x_display=:128");
  writeState(oversized_display);
  EXPECT_FALSE(proc::prism_read_headless_state(state_path, "42").has_value());
}

TEST_F(HeadlessStateTest, RejectsWrongOwnedUnitsAndOutput) {
  auto wrong_unit = validState();
  wrong_unit.replace(
    wrong_unit.find("steam_unit=prism-headless-steam.service"),
    std::string("steam_unit=prism-headless-steam.service").size(),
    "steam_unit=steam.service"
  );
  writeState(wrong_unit);
  EXPECT_FALSE(proc::prism_read_headless_state(state_path, "42").has_value());

  auto wrong_output = validState();
  wrong_output.replace(
    wrong_output.find("output_name=HEADLESS-1"),
    std::string("output_name=HEADLESS-1").size(),
    "output_name=HEADLESS-2"
  );
  writeState(wrong_output);
  EXPECT_FALSE(proc::prism_read_headless_state(state_path, "42").has_value());
}

TEST(WlrootsCaptureOverrideTest, AcceptsExactContract) {
  const auto session = proc::prism_parse_wlroots_capture_override("wlroots:session-42");
  ASSERT_TRUE(session.has_value());
  EXPECT_EQ(*session, "session-42");
}

TEST(WlrootsCaptureOverrideTest, RejectsMalformedOrDifferentContracts) {
  EXPECT_FALSE(proc::prism_parse_wlroots_capture_override("").has_value());
  EXPECT_FALSE(proc::prism_parse_wlroots_capture_override("portal:HEADLESS-1").has_value());
  EXPECT_FALSE(proc::prism_parse_wlroots_capture_override("wlroots:").has_value());
  EXPECT_FALSE(proc::prism_parse_wlroots_capture_override("wlroots:bad/session").has_value());
  EXPECT_FALSE(proc::prism_parse_wlroots_capture_override("wlroots:session\nextra").has_value());
  EXPECT_FALSE(proc::prism_parse_wlroots_capture_override("wlroots:" + std::string(129, 'a')).has_value());
}

/**
 * @brief Exercise capture validation with isolated virtual-display lifecycle helpers.
 */
class CaptureLaunchTest: public SessionCleanupTest {
protected:
  /**
   * @brief Install synthetic capture helpers without changing host displays.
   */
  void SetUp() override {
    SessionCleanupTest::SetUp();
    writeHelper("prism-virtual-start.sh", "touch capture-ready");
    writeHelper("prism-virtual-stop.sh", "rm -f capture-ready; touch capture-stopped");
    writeHelper("prepare.sh", "touch prep-ready");
    writeHelper("undo.sh", "rm -f prep-ready; touch prep-undone");
    writeHelper("app.sh", "touch app-started");
  }

  /**
   * @brief Write a helper whose marker files stay inside the test directory.
   *
   * @param name Helper filename.
   * @param command Shell commands to run after entering the test directory.
   */
  void writeHelper(const std::string &name, const std::string &command) const {
    const auto path = test_dir / name;
    std::ofstream file(path);
    file << "#!/bin/sh\ncd \"$(dirname \"$0\")\"\n"
         << command << '\n';
    file.close();
    fs::permissions(path, fs::perms::owner_all);
  }

  /**
   * @brief Build an application with reversible prep and a virtual capture mode.
   *
   * @return Isolated process manager.
   */
  proc::proc_t makeProcess() const {
    proc::ctx_t app {};
    app.id = "123";
    app.name = "Capture validation test";
    app.prism_capture = "virtual";
    app.prep_cmds.emplace_back((test_dir / "prepare.sh").string(), (test_dir / "undo.sh").string(), false);
    return proc::proc_t(boost::this_process::environment(), {app});
  }
};

/**
 * @brief Verify that capture validation observes prepared outputs before application commands start.
 */
TEST_F(CaptureLaunchTest, ValidatesAfterCaptureAndPrepBeforeLaunchingCommands) {
  auto process = makeProcess();
  process.get_apps()[0].cmd = (test_dir / "app.sh").string();
  process.get_apps()[0].auto_detach = false;
  int probes = 0;
  EXPECT_EQ(process.execute(123, std::make_shared<rtsp_stream::launch_session_t>(), [&]() {
    ++probes;
    EXPECT_TRUE(fs::exists(test_dir / "capture-ready"));
    EXPECT_TRUE(fs::exists(test_dir / "prep-ready"));
    EXPECT_FALSE(fs::exists(test_dir / "app-started"));
    return 0;
  }),
            0);
  process.terminate();
  EXPECT_EQ(probes, 1);
  EXPECT_TRUE(fs::exists(test_dir / "capture-stopped"));
  EXPECT_TRUE(fs::exists(test_dir / "prep-undone"));
}

/**
 * @brief Verify failed validation restores resources and allows the next launch to succeed.
 */
TEST_F(CaptureLaunchTest, FailedProbeRollsBackAndAllowsRetry) {
  auto process = makeProcess();
  process.get_apps()[0].cmd = (test_dir / "app.sh").string();
  process.get_apps()[0].detached.push_back((test_dir / "app.sh").string());
  EXPECT_EQ(process.execute(123, std::make_shared<rtsp_stream::launch_session_t>(), []() {
    return 503;
  }),
            503);
  EXPECT_EQ(process.running(), 0);
  EXPECT_FALSE(fs::exists(test_dir / "capture-ready"));
  EXPECT_FALSE(fs::exists(test_dir / "prep-ready"));
  EXPECT_FALSE(fs::exists(test_dir / "app-started"));
  EXPECT_TRUE(fs::exists(test_dir / "capture-stopped"));
  EXPECT_TRUE(fs::exists(test_dir / "prep-undone"));

  process.get_apps()[0].cmd.clear();
  process.get_apps()[0].detached.clear();
  EXPECT_EQ(process.execute(123, std::make_shared<rtsp_stream::launch_session_t>(), []() {
    return 0;
  }),
            0);
  EXPECT_EQ(process.running(), 123);
  process.terminate();
}

/**
 * @brief Verify partial capture setup is rolled back without attempting validation.
 */
TEST_F(CaptureLaunchTest, CaptureFailureSkipsProbeAndCleansUp) {
  writeHelper("prism-virtual-start.sh", "touch capture-ready; exit 1");
  auto process = makeProcess();
  EXPECT_EQ(process.execute(123, std::make_shared<rtsp_stream::launch_session_t>(), []() {
    ADD_FAILURE() << "Capture validation ran before capture setup succeeded";
    return 0;
  }),
            -1);
  EXPECT_FALSE(fs::exists(test_dir / "capture-ready"));
  EXPECT_FALSE(fs::exists(test_dir / "prep-ready"));
  EXPECT_TRUE(fs::exists(test_dir / "capture-stopped"));
}

/**
 * @brief Verify a failed preparation command prevents capture validation.
 */
TEST_F(CaptureLaunchTest, PrepFailureSkipsProbe) {
  writeHelper("prepare.sh", "exit 1");
  auto process = makeProcess();
  EXPECT_EQ(process.execute(123, std::make_shared<rtsp_stream::launch_session_t>(), []() {
    ADD_FAILURE() << "Capture validation ran after failed prep";
    return 0;
  }),
            -1);
  EXPECT_FALSE(fs::exists(test_dir / "capture-ready"));
  EXPECT_TRUE(fs::exists(test_dir / "capture-stopped"));
}

/**
 * @brief Preserve application launches that do not request capture validation.
 */
TEST_F(CaptureLaunchTest, SupportsLaunchWithoutValidation) {
  auto process = makeProcess();
  EXPECT_EQ(process.execute(123, std::make_shared<rtsp_stream::launch_session_t>()), 0);
  EXPECT_EQ(process.running(), 123);
  process.terminate();
}
