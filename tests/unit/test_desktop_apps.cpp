/**
 * @file tests/unit/test_desktop_apps.cpp
 * @brief Test src/desktop_apps.* functions.
 */
// test imports
#include "../tests_common.h"

// standard imports
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

// local imports
#include <src/desktop_apps.h>

namespace fs = std::filesystem;

namespace {
  /**
   * @brief Set an environment variable for the lifetime of the guard.
   */
  class EnvGuard {
  public:
    /**
     * @brief Set or unset a variable, remembering its previous value.
     *
     * @param name Variable name.
     * @param value New value, or std::nullopt to unset it.
     */
    EnvGuard(std::string name, const std::optional<std::string> &value):
        name_(std::move(name)) {
      if (const char *old = std::getenv(name_.c_str())) {
        old_ = old;
      }
      if (value) {
        setenv(name_.c_str(), value->c_str(), 1);
      } else {
        unsetenv(name_.c_str());
      }
    }

    /**
     * @brief Guards own the environment change, so copying is not allowed.
     */
    EnvGuard(const EnvGuard &) = delete;

    /**
     * @brief Guards own the environment change, so assignment is not allowed.
     *
     * @return Never returns.
     */
    EnvGuard &operator=(const EnvGuard &) = delete;

    /**
     * @brief Restore the variable to its value before the guard was created.
     */
    ~EnvGuard() {
      if (old_) {
        setenv(name_.c_str(), old_->c_str(), 1);
      } else {
        unsetenv(name_.c_str());
      }
    }

  private:
    std::string name_;  ///< Variable name.
    std::optional<std::string> old_;  ///< Value to restore.
  };
}  // namespace

class DesktopAppsTest: public BaseTest {
protected:
  void SetUp() override {
    BaseTest::SetUp();
    test_dir = fs::temp_directory_path() / "prism_desktop_apps_test";  // NOSONAR(cpp:S5443): safe for tests
    fs::remove_all(test_dir);
    user = test_dir / "user";
    system = test_dir / "system";
    fs::create_directories(user / "applications");
    fs::create_directories(system / "applications");
  }

  void TearDown() override {
    fs::remove_all(test_dir);
    BaseTest::TearDown();
  }

  /**
   * @brief Write a file, creating its directory.
   *
   * @param path File to write.
   * @param content File content.
   */
  static void write_file(const fs::path &path, const std::string &content) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << content;
  }

  /**
   * @brief Write a desktop entry with the given keys in its `[Desktop Entry]` group.
   *
   * @param dir Data directory (the entry goes into its `applications` folder).
   * @param id Desktop file name, optionally with sub-directories.
   * @param keys Lines to place in the group.
   */
  static void write_entry(const fs::path &dir, const std::string &id, const std::string &keys) {
    write_file(dir / "applications" / id, "[Desktop Entry]\n" + keys + "\n");
  }

  /**
   * @brief Write a minimal valid application entry.
   *
   * @param dir Data directory.
   * @param id Desktop file name.
   * @param name Display name.
   * @param extra Additional lines for the group.
   */
  static void write_app(const fs::path &dir, const std::string &id, const std::string &name, const std::string &extra = "") {
    write_entry(dir, id, "Type=Application\nName=" + name + "\nExec=" + id + "-bin %U\n" + extra);
  }

  /**
   * @brief List the names of the discovered applications.
   *
   * @param apps Discovered applications.
   * @return Names in order.
   */
  static std::vector<std::string> names(const std::vector<prism::desktop::app_t> &apps) {
    std::vector<std::string> result;
    for (const auto &app : apps) {
      result.push_back(app.name);
    }
    return result;
  }

  /**
   * @brief Check whether an application with the given name was discovered.
   *
   * @param apps Discovered applications.
   * @param name Display name to look for.
   * @return True when the application is listed.
   */
  static bool contains(const std::vector<prism::desktop::app_t> &apps, const std::string &name) {
    return std::any_of(apps.begin(), apps.end(), [&name](const auto &app) {
      return app.name == name;
    });
  }

  fs::path test_dir;  ///< Temporary root for the test.
  fs::path user;  ///< Highest-precedence data directory.
  fs::path system;  ///< Lower-precedence data directory.
};

TEST(DesktopExecCommand, RemovesFieldCodes) {
  EXPECT_EQ(prism::desktop::exec_command("firefox %u"), "firefox");
  EXPECT_EQ(prism::desktop::exec_command("viewer %F %i %c %k"), "viewer");
  EXPECT_EQ(prism::desktop::exec_command("app --file=%f --name %c"), "app --file= --name");
  EXPECT_EQ(prism::desktop::exec_command("app 100%%"), "app 100%");
  EXPECT_EQ(prism::desktop::exec_command("app 50%"), "app 50%");
}

TEST(DesktopExecCommand, RemovesFlatpakFileMarkers) {
  EXPECT_EQ(
    prism::desktop::exec_command("/usr/bin/flatpak run --file-forwarding org.example.App @@u %U @@"),
    "/usr/bin/flatpak run --file-forwarding org.example.App"
  );
}

TEST(DesktopExecCommand, QuotesArgumentsForTheShell) {
  EXPECT_EQ(prism::desktop::exec_command(R"("/opt/My App/run" --title "a b")"), "'/opt/My App/run' --title 'a b'");
  EXPECT_EQ(prism::desktop::exec_command(R"(app "it's")"), R"(app 'it'\''s')");
  EXPECT_EQ(prism::desktop::exec_command(R"(app "say \"hi\" \$HOME \\ \`x\`")"), R"(app 'say "hi" $HOME \ `x`')");
  EXPECT_EQ(prism::desktop::exec_command("env\tA=b   app"), "env A=b app");
  EXPECT_EQ(prism::desktop::exec_command(R"(app "")"), "app ''");
  EXPECT_EQ(prism::desktop::exec_command(R"(app "unterminated arg)"), "app 'unterminated arg'");
  EXPECT_EQ(prism::desktop::exec_command(R"(app "a\nb")"), R"(app 'a\nb')");
  EXPECT_EQ(prism::desktop::exec_command(R"cmd(app "$(touch x)")cmd"), "app '$(touch x)'");
}

TEST(DesktopExecCommand, EmptyWhenNothingToRun) {
  EXPECT_EQ(prism::desktop::exec_command(""), "");
  EXPECT_EQ(prism::desktop::exec_command("   "), "");
  EXPECT_EQ(prism::desktop::exec_command("%U"), "");
}

TEST_F(DesktopAppsTest, ListsVisibleApplicationsSortedByName) {
  write_app(user, "zeta.desktop", "zeta");
  write_app(user, "alpha.desktop", "Beta");
  write_app(system, "gamma.desktop", "Alpha");

  const auto apps = prism::desktop::installed_apps({user, system}, "");
  EXPECT_EQ(names(apps), (std::vector<std::string> {"Alpha", "Beta", "zeta"}));
  EXPECT_EQ(apps[0].command, "gamma.desktop-bin");
}

TEST_F(DesktopAppsTest, SortsNamesThatDifferOnlyByCase) {
  write_app(user, "lower.desktop", "app");
  write_app(user, "upper.desktop", "App");

  EXPECT_EQ(names(prism::desktop::installed_apps({user}, "")), (std::vector<std::string> {"App", "app"}));
}

TEST_F(DesktopAppsTest, SkipsEntriesThatAreNotLaunchableApplications) {
  write_app(user, "shown.desktop", "Shown");
  write_app(user, "nodisplay.desktop", "No Display", "NoDisplay=true");
  write_app(user, "hidden.desktop", "Hidden", "Hidden=true");
  write_app(user, "terminal.desktop", "Terminal", "Terminal=true");
  write_entry(user, "link.desktop", "Type=Link\nName=Link\nURL=https://example.com");
  write_entry(user, "typeless.desktop", "Name=Typeless\nExec=typeless");
  write_entry(user, "noexec.desktop", "Type=Application\nName=No Exec");
  write_entry(user, "onlycodes.desktop", "Type=Application\nName=Only Codes\nExec=%U");
  write_entry(user, "noname.desktop", "Type=Application\nExec=noname");
  write_file(user / "applications" / "readme.txt", "[Desktop Entry]\nType=Application\nName=Text\nExec=text\n");

  EXPECT_EQ(names(prism::desktop::installed_apps({user}, "")), (std::vector<std::string> {"Shown"}));
}

TEST_F(DesktopAppsTest, ParsesOnlyTheDesktopEntryGroup) {
  write_file(
    user / "applications" / "grouped.desktop",
    "# comment\n"
    "[Desktop Action new-window]\n"
    "Name=Wrong\n"
    "Exec=wrong\n"
    "\n"
    "[Desktop Entry]\n"
    "Type=Application\n"
    "Name[de]=Anwendung\n"
    "Name = Real App \n"
    "Name=Duplicate\n"
    "Exec=real\n"
    "not a key value line\n"
    "\n"
    "[Desktop Action other]\n"
    "Name=Also Wrong\n"
    "Exec=also-wrong\n"
  );

  const auto apps = prism::desktop::installed_apps({user}, "");
  ASSERT_EQ(apps.size(), 1);
  EXPECT_EQ(apps[0].name, "Real App");
  EXPECT_EQ(apps[0].command, "real");
}

TEST_F(DesktopAppsTest, HonorsOnlyShowInAndNotShowIn) {
  write_app(user, "everywhere.desktop", "Everywhere");
  write_app(user, "kde-only.desktop", "KDE Only", "OnlyShowIn=KDE;");
  write_app(user, "gnome-only.desktop", "GNOME Only", "OnlyShowIn=GNOME;Unity;");
  write_app(user, "not-kde.desktop", "Not KDE", "NotShowIn=KDE;");
  write_app(user, "not-gnome.desktop", "Not GNOME", "NotShowIn=GNOME;");

  EXPECT_EQ(
    names(prism::desktop::installed_apps({user}, "KDE")),
    (std::vector<std::string> {"Everywhere", "KDE Only", "Not GNOME"})
  );
  EXPECT_EQ(
    names(prism::desktop::installed_apps({user}, "ubuntu:GNOME")),
    (std::vector<std::string> {"Everywhere", "GNOME Only", "Not KDE"})
  );
  EXPECT_EQ(
    names(prism::desktop::installed_apps({user}, "")),
    (std::vector<std::string> {"Everywhere", "Not GNOME", "Not KDE"})
  );
}

TEST_F(DesktopAppsTest, ChecksTryExec) {
  const fs::path runnable = test_dir / "bin" / "runnable";
  const fs::path plain = test_dir / "bin" / "plain";
  write_file(runnable, "#!/bin/sh\n");
  write_file(plain, "data\n");
  fs::permissions(runnable, fs::perms::owner_exec, fs::perm_options::add);
  fs::permissions(plain, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::remove);

  write_app(user, "absolute-ok.desktop", "Absolute OK", "TryExec=" + runnable.string());
  write_app(user, "absolute-plain.desktop", "Absolute Plain", "TryExec=" + plain.string());
  write_app(user, "absolute-missing.desktop", "Absolute Missing", "TryExec=" + (test_dir / "bin" / "missing").string());
  write_app(user, "path-ok.desktop", "Path OK", "TryExec=runnable");
  write_app(user, "path-missing.desktop", "Path Missing", "TryExec=prism-no-such-program");

  {
    EnvGuard path("PATH", (test_dir / "bin").string());
    EXPECT_EQ(
      names(prism::desktop::installed_apps({user}, "")),
      (std::vector<std::string> {"Absolute OK", "Path OK"})
    );
  }
  {
    EnvGuard path("PATH", std::nullopt);
    EXPECT_EQ(names(prism::desktop::installed_apps({user}, "")), (std::vector<std::string> {"Absolute OK"}));
  }
}

TEST_F(DesktopAppsTest, EarlierDirectoriesTakePrecedence) {
  write_app(user, "shared.desktop", "User Shared");
  write_app(system, "shared.desktop", "System Shared");
  write_app(user, "masked.desktop", "Masked User", "NoDisplay=true");
  write_app(system, "masked.desktop", "Masked System");
  write_app(system, "system-only.desktop", "System Only");

  EXPECT_EQ(
    names(prism::desktop::installed_apps({user, system}, "")),
    (std::vector<std::string> {"System Only", "User Shared"})
  );
}

TEST_F(DesktopAppsTest, FindsEntriesInSubdirectories) {
  write_app(user, "vendor/tool.desktop", "Tool");
  write_app(user, "vendor/nested/deep.desktop", "Deep");

  EXPECT_EQ(names(prism::desktop::installed_apps({user}, "")), (std::vector<std::string> {"Deep", "Tool"}));
}

TEST_F(DesktopAppsTest, SubdirectoryEntryHasTheDashedDesktopFileId) {
  write_app(system, "vendor-tool.desktop", "Flat Tool");
  write_app(user, "vendor/tool.desktop", "Nested Tool");

  EXPECT_EQ(names(prism::desktop::installed_apps({user, system}, "")), (std::vector<std::string> {"Nested Tool"}));
}

TEST_F(DesktopAppsTest, KeepsTheFirstApplicationWithADuplicateName) {
  write_app(user, "a.desktop", "Dup");
  write_app(user, "b.desktop", "Dup");
  write_app(system, "c.desktop", "Dup");

  const auto apps = prism::desktop::installed_apps({user, system}, "");
  ASSERT_EQ(apps.size(), 1);
  EXPECT_EQ(apps[0].command, "a.desktop-bin");
}

TEST_F(DesktopAppsTest, FollowsSymlinkedEntriesAndIgnoresBrokenOnes) {
  write_app(test_dir, "real.desktop", "Linked");
  fs::create_symlink(test_dir / "applications" / "real.desktop", user / "applications" / "linked.desktop");
  fs::create_symlink(test_dir / "applications" / "gone.desktop", user / "applications" / "broken.desktop");
  write_app(user, "zzz.desktop", "After Broken Link");

  EXPECT_EQ(
    names(prism::desktop::installed_apps({user}, "")),
    (std::vector<std::string> {"After Broken Link", "Linked"})
  );
}

TEST_F(DesktopAppsTest, ReportsTheDesktopFileIdAndFlatpakOrigin) {
  const fs::path exports = test_dir / "flatpak" / "exports" / "share";
  write_app(user, "native.desktop", "Native");
  write_app(user, "vendor/nested.desktop", "Nested");
  write_app(user, "exported.desktop", "Exported", "X-Flatpak=org.example.Exported");
  write_app(exports, "org.example.Other.desktop", "Other");

  const auto apps = prism::desktop::installed_apps({user, exports}, "");
  ASSERT_EQ(apps.size(), 4);

  EXPECT_EQ(apps[0].name, "Exported");
  EXPECT_EQ(apps[0].id, "exported");
  EXPECT_TRUE(apps[0].flatpak);  // X-Flatpak key

  EXPECT_EQ(apps[1].name, "Native");
  EXPECT_EQ(apps[1].id, "native");
  EXPECT_FALSE(apps[1].flatpak);

  EXPECT_EQ(apps[2].name, "Nested");
  EXPECT_EQ(apps[2].id, "vendor-nested");
  EXPECT_FALSE(apps[2].flatpak);

  EXPECT_EQ(apps[3].name, "Other");
  EXPECT_EQ(apps[3].id, "org.example.Other");
  EXPECT_TRUE(apps[3].flatpak);  // flatpak/exports directory
}

TEST_F(DesktopAppsTest, MissingDirectoriesReturnEmpty) {
  EXPECT_TRUE(prism::desktop::installed_apps({}, "").empty());
  EXPECT_TRUE(prism::desktop::installed_apps({test_dir / "nope"}, "").empty());
}

TEST_F(DesktopAppsTest, FindsThemeIcons) {
  write_file(user / "icons" / "hicolor" / "48x48" / "apps" / "themed.png", "small");
  write_file(user / "icons" / "hicolor" / "256x256" / "apps" / "themed.png", "large");
  write_file(system / "pixmaps" / "pixmap.png", "pixmap");
  write_file(system / "pixmaps" / "Explicit.PNG", "explicit");
  write_file(system / "icons" / "hicolor" / "scalable" / "apps" / "vector.svg", "<svg/>");

  write_app(user, "themed.desktop", "Themed", "Icon=themed");
  write_app(user, "pixmap.desktop", "Pixmap", "Icon=pixmap");
  write_app(user, "explicit.desktop", "Explicit", "Icon=Explicit.PNG");
  write_app(user, "vector.desktop", "Vector", "Icon=vector");
  write_app(user, "missing.desktop", "Missing", "Icon=not-installed");
  write_app(user, "none.desktop", "None");

  const auto apps = prism::desktop::installed_apps({user, system}, "");
  ASSERT_EQ(apps.size(), 6);
  EXPECT_EQ(apps[0].name, "Explicit");
  EXPECT_EQ(apps[0].icon, system / "pixmaps" / "Explicit.PNG");
  EXPECT_TRUE(apps[1].icon.empty());  // Missing
  EXPECT_TRUE(apps[2].icon.empty());  // None
  EXPECT_EQ(apps[3].name, "Pixmap");
  EXPECT_EQ(apps[3].icon, system / "pixmaps" / "pixmap.png");
  EXPECT_EQ(apps[4].name, "Themed");
  EXPECT_EQ(apps[4].icon, user / "icons" / "hicolor" / "256x256" / "apps" / "themed.png");
  EXPECT_TRUE(apps[5].icon.empty());  // Vector (SVG only)
}

TEST_F(DesktopAppsTest, AcceptsOnlyAbsolutePngIconPaths) {
  const fs::path png = test_dir / "art" / "icon.png";
  const fs::path svg = test_dir / "art" / "icon.svg";
  write_file(png, "png");
  write_file(svg, "svg");
  write_file(user / "icons" / "hicolor" / "256x256" / "apps" / "nested" / "relative.png", "png");

  write_app(user, "png.desktop", "Png", "Icon=" + png.string());
  write_app(user, "svg.desktop", "Svg", "Icon=" + svg.string());
  write_app(user, "gone.desktop", "Gone", "Icon=" + (test_dir / "art" / "gone.png").string());
  write_app(user, "relative.desktop", "Relative", "Icon=nested/relative");

  const auto apps = prism::desktop::installed_apps({user}, "");
  ASSERT_EQ(apps.size(), 4);
  EXPECT_TRUE(apps[0].icon.empty());  // Gone
  EXPECT_EQ(apps[1].icon, png);
  EXPECT_TRUE(apps[2].icon.empty());  // Relative path with a separator
  EXPECT_TRUE(apps[3].icon.empty());  // Svg
}

TEST_F(DesktopAppsTest, DataDirsFollowTheXdgSpecification) {
  EnvGuard home("HOME", "/home/tester");
  EnvGuard data_home("XDG_DATA_HOME", "/data/home");
  EnvGuard data_dirs("XDG_DATA_DIRS", "/data/a:/data/home:/data/b::/data/a");

  EXPECT_EQ(
    prism::desktop::data_dirs(),
    (std::vector<fs::path> {"/data/home", "/data/a", "/data/b"})
  );
}

TEST_F(DesktopAppsTest, DataDirsFallBackToSpecificationDefaults) {
  EnvGuard home("HOME", "/home/tester");
  EnvGuard data_home("XDG_DATA_HOME", "");
  EnvGuard data_dirs("XDG_DATA_DIRS", std::nullopt);

  EXPECT_EQ(
    prism::desktop::data_dirs(),
    (std::vector<fs::path> {
      "/home/tester/.local/share",
      "/usr/local/share",
      "/usr/share",
      "/home/tester/.local/share/flatpak/exports/share",
      "/var/lib/flatpak/exports/share",
    })
  );
}

TEST_F(DesktopAppsTest, DataDirsWithoutHome) {
  EnvGuard home("HOME", std::nullopt);
  EnvGuard data_home("XDG_DATA_HOME", std::nullopt);
  EnvGuard data_dirs("XDG_DATA_DIRS", std::nullopt);

  EXPECT_EQ(
    prism::desktop::data_dirs(),
    (std::vector<fs::path> {"/usr/local/share", "/usr/share", "/var/lib/flatpak/exports/share"})
  );

  EnvGuard listed("XDG_DATA_DIRS", "/data/a");
  EXPECT_EQ(prism::desktop::data_dirs(), (std::vector<fs::path> {"/data/a"}));
}

TEST_F(DesktopAppsTest, InstalledAppsUsesTheEnvironment) {
  write_app(user, "user.desktop", "User App");
  write_app(system, "system.desktop", "System App");
  write_app(system, "kde.desktop", "KDE App", "OnlyShowIn=KDE;");

  EnvGuard data_home("XDG_DATA_HOME", user.string());
  EnvGuard data_dirs("XDG_DATA_DIRS", system.string());
  {
    EnvGuard desktop("XDG_CURRENT_DESKTOP", "KDE");
    const auto apps = prism::desktop::installed_apps();
    EXPECT_TRUE(contains(apps, "User App"));
    EXPECT_TRUE(contains(apps, "System App"));
    EXPECT_TRUE(contains(apps, "KDE App"));
  }
  {
    EnvGuard desktop("XDG_CURRENT_DESKTOP", std::nullopt);
    const auto apps = prism::desktop::installed_apps();
    EXPECT_TRUE(contains(apps, "User App"));
    EXPECT_TRUE(contains(apps, "System App"));
    EXPECT_FALSE(contains(apps, "KDE App"));
  }
}
