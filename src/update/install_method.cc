#include "update/install_method.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <string_view>
#include <utility>

namespace mocktail::update {
namespace {

constexpr std::uintmax_t kMaximumMetadataFileBytes = 64U * 1024U;
constexpr std::string_view kFlatpakAppId = "space.bigrat.mocktail";

bool IsRegularFile(const std::filesystem::path& path) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  return !error && std::filesystem::is_regular_file(status);
}

bool IsDirectory(const std::filesystem::path& path) {
  std::error_code error;
  return std::filesystem::is_directory(path, error);
}

std::vector<std::string> ReadLines(const std::filesystem::path& path) {
  std::vector<std::string> lines;
  std::error_code error;
  const std::uintmax_t size = std::filesystem::file_size(path, error);
  if (error || size > kMaximumMetadataFileBytes) return lines;
  std::ifstream input(path);
  for (std::string line; std::getline(input, line);) {
    lines.push_back(std::move(line));
  }
  return lines;
}

std::string Unquote(std::string_view value) {
  if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') &&
      value.back() == value.front()) {
    value = value.substr(1, value.size() - 2);
  }
  return std::string(value);
}

std::vector<std::string> SplitWords(std::string_view value) {
  std::vector<std::string> words;
  std::size_t begin = 0;
  while (begin < value.size()) {
    const std::size_t end = value.find(' ', begin);
    const std::string_view word = value.substr(
        begin, end == std::string_view::npos ? std::string_view::npos
                                             : end - begin);
    if (!word.empty()) words.emplace_back(word);
    if (end == std::string_view::npos) break;
    begin = end + 1;
  }
  return words;
}

void ReadOsRelease(const std::filesystem::path& path, InstallFacts* facts) {
  for (const std::string& line : ReadLines(path)) {
    const std::size_t equals = line.find('=');
    if (equals == std::string::npos || line.front() == '#') continue;
    const std::string_view key(line.data(), equals);
    const std::string value =
        Unquote(std::string_view(line).substr(equals + 1));
    if (key == "ID") {
      facts->os_id = value;
    } else if (key == "ID_LIKE") {
      facts->os_id_like = SplitWords(value);
    } else if (key == "VERSION_ID") {
      facts->os_version_id = value;
    } else if (key == "VARIANT_ID") {
      facts->os_variant_id = value;
    } else if (key == "PRETTY_NAME") {
      facts->os_pretty_name = value;
    }
  }
}

std::string FlatpakApplicationName(const std::filesystem::path& info) {
  bool application_section = false;
  for (const std::string& line : ReadLines(info)) {
    if (!line.empty() && line.front() == '[') {
      application_section = line == "[Application]";
    } else if (application_section && line.rfind("name=", 0) == 0) {
      return line.substr(5);
    }
  }
  return {};
}

bool StartsWithMocktail(std::string_view name) {
  return name.rfind("mocktail", 0) == 0;
}

// pacman keeps one "NAME-VERSION-RELEASE" directory per installed package.
std::vector<std::string> PacmanPackages(const std::filesystem::path& local) {
  std::vector<std::string> packages;
  std::error_code error;
  for (std::filesystem::directory_iterator iterator(local, error), end;
       !error && iterator != end; iterator.increment(error)) {
    std::string name = iterator->path().filename().string();
    if (!StartsWithMocktail(name)) continue;
    for (int component = 0; component < 2; ++component) {
      const std::size_t dash = name.rfind('-');
      if (dash == std::string::npos) break;
      name.erase(dash);
    }
    if (!name.empty()) packages.push_back(std::move(name));
  }
  std::sort(packages.begin(), packages.end());
  return packages;
}

// dpkg keeps "NAME.list" or "NAME:ARCH.list" for each installed package.
std::vector<std::string> DpkgPackages(const std::filesystem::path& info) {
  std::vector<std::string> packages;
  std::error_code error;
  for (std::filesystem::directory_iterator iterator(info, error), end;
       !error && iterator != end; iterator.increment(error)) {
    const std::filesystem::path& path = iterator->path();
    if (path.extension() != ".list") continue;
    std::string name = path.stem().string();
    if (!StartsWithMocktail(name)) continue;
    const std::size_t colon = name.find(':');
    if (colon != std::string::npos) name.erase(colon);
    packages.push_back(std::move(name));
  }
  std::sort(packages.begin(), packages.end());
  return packages;
}

bool UnderPrefix(const std::filesystem::path& path, std::string_view prefix) {
  const std::string text = path.lexically_normal().string();
  return text.rfind(prefix, 0) == 0;
}

// "26.04" -> 2604, "44" -> 4400. Anything unparsable is 0.
int NumericVersion(std::string_view value) {
  int major = 0;
  int minor = 0;
  const char* end = value.data() + value.size();
  auto parsed = std::from_chars(value.data(), end, major);
  if (parsed.ec != std::errc()) return 0;
  if (parsed.ptr != end && *parsed.ptr == '.') {
    parsed = std::from_chars(parsed.ptr + 1, end, minor);
    if (parsed.ec != std::errc()) minor = 0;
  }
  return major * 100 + std::min(minor, 99);
}

bool IdLike(const InstallFacts& facts, std::string_view id) {
  return facts.os_id == id ||
         std::find(facts.os_id_like.begin(), facts.os_id_like.end(), id) !=
             facts.os_id_like.end();
}

// Only the systems the project publishes native packages for. Image-based
// systems are excluded: their package changes do not survive an OS update,
// so Flatpak is the right way to install Mocktail there.
void DescribeNativeAlternative(const InstallFacts& facts,
                               const std::string& project_url,
                               InstallMethod* method) {
  constexpr std::string_view kDataNote =
      " Settings, sign-in, and the downloaded Roblox copy stay in "
      "~/.var/app/space.bigrat.mocktail and are not moved; the native package "
      "downloads Roblox again and asks you to sign in again.";
  const bool image_based =
      facts.os_id == "steamos" || facts.os_id == "bazzite" ||
      facts.os_variant_id == "silverblue" || facts.os_variant_id == "kinoite" ||
      facts.os_variant_id == "sericea" || facts.os_variant_id == "onyx" ||
      facts.os_variant_id.find("atomic") != std::string::npos;
  if (image_based) return;
  const std::string system =
      facts.os_pretty_name.empty() ? facts.os_id : facts.os_pretty_name;
  if (IdLike(facts, "arch")) {
    method->alternative =
        system + " can use the prebuilt mocktail-bin package from the AUR "
                 "instead of the Flatpak. It is updated with the rest of the "
                 "system." +
        std::string(kDataNote);
    method->alternative_command =
        "paru -S mocktail-bin && flatpak uninstall space.bigrat.mocktail";
  } else if (facts.os_id == "fedora" &&
             NumericVersion(facts.os_version_id) >= NumericVersion("44")) {
    method->alternative =
        system + " can use Mocktail's DNF repository instead of the Flatpak. "
                 "It is updated with the rest of the system." +
        std::string(kDataNote);
    method->alternative_command =
        "sudo curl -fsSL https://mocktail.bigrat.space/rpm/mocktail.repo -o "
        "/etc/yum.repos.d/mocktail.repo && sudo dnf install mocktail && "
        "flatpak uninstall space.bigrat.mocktail";
  } else if (facts.os_id == "ubuntu" &&
             NumericVersion(facts.os_version_id) >= NumericVersion("26.04")) {
    method->alternative =
        system + " can use Mocktail's APT repository instead of the Flatpak. "
                 "It is updated with the rest of the system." +
        std::string(kDataNote);
    method->alternative_command = project_url + "#install-with-apt";
  }
}

}  // namespace

const char* InstallChannelName(InstallChannel channel) {
  switch (channel) {
    case InstallChannel::kFlatpak:
      return "flatpak";
    case InstallChannel::kAppImage:
      return "appimage";
    case InstallChannel::kNix:
      return "nix";
    case InstallChannel::kPacman:
      return "pacman";
    case InstallChannel::kDpkg:
      return "dpkg";
    case InstallChannel::kRpm:
      return "rpm";
    case InstallChannel::kSourceBuild:
      return "source-build";
    case InstallChannel::kUnknown:
      break;
  }
  return "unknown";
}

InstallFacts GatherInstallFacts(const std::filesystem::path& root,
                                const std::filesystem::path& executable,
                                const std::string& appimage_path) {
  InstallFacts facts;
  facts.executable = executable;
  facts.appimage_path = appimage_path;
  const std::filesystem::path flatpak_info = root / ".flatpak-info";
  facts.in_flatpak = IsRegularFile(flatpak_info);
  if (facts.in_flatpak) {
    facts.flatpak_app_id = FlatpakApplicationName(flatpak_info);
    ReadOsRelease(root / "run/host/os-release", &facts);
  } else {
    const std::filesystem::path etc_release = root / "etc/os-release";
    ReadOsRelease(IsRegularFile(etc_release) ? etc_release
                                             : root / "usr/lib/os-release",
                  &facts);
  }
  if (!executable.empty()) {
    facts.build_tree =
        IsRegularFile(executable.parent_path() / "CMakeCache.txt");
  }
  facts.pacman_packages = PacmanPackages(root / "var/lib/pacman/local");
  facts.dpkg_packages = DpkgPackages(root / "var/lib/dpkg/info");
  facts.rpm_database = IsDirectory(root / "var/lib/rpm") ||
                       IsDirectory(root / "usr/lib/sysimage/rpm");
  facts.apt_repository =
      IsRegularFile(root / "etc/apt/sources.list.d/mocktail.list") ||
      IsRegularFile(root / "etc/apt/sources.list.d/mocktail.sources");
  facts.dnf_repository = IsRegularFile(root / "etc/yum.repos.d/mocktail.repo");
  return facts;
}

InstallMethod DescribeInstallMethod(const InstallFacts& facts,
                                    const std::string& project_url,
                                    const std::string& release_url) {
  InstallMethod method;
  if (facts.in_flatpak) {
    method.channel = InstallChannel::kFlatpak;
    method.package = facts.flatpak_app_id.empty()
                         ? std::string(kFlatpakAppId)
                         : facts.flatpak_app_id;
    method.description = "Flatpak (" + method.package + ")";
    method.update_instructions =
        "Flatpak installs Mocktail updates, and a new release can take a few "
        "hours to reach Flathub. Use your software center, or run:";
    method.update_command = "flatpak update " + method.package;
    DescribeNativeAlternative(facts, project_url, &method);
    return method;
  }
  if (!facts.appimage_path.empty()) {
    method.channel = InstallChannel::kAppImage;
    method.description = "AppImage (" + facts.appimage_path + ")";
    method.update_instructions =
        "This AppImage does not replace itself. Download the new AppImage and "
        "use it instead of " + facts.appimage_path + ":";
    method.update_command = release_url;
    return method;
  }
  if (UnderPrefix(facts.executable, "/nix/store/")) {
    method.channel = InstallChannel::kNix;
    method.description = "Nix";
    method.update_instructions =
        "Nix manages this installation. Update the flake input or channel "
        "that provides Mocktail, then rebuild.";
    return method;
  }
  if (facts.build_tree) {
    method.channel = InstallChannel::kSourceBuild;
    method.description = "source build (" +
                         facts.executable.parent_path().string() + ")";
    method.update_instructions =
        "This is a local build. Update the checkout and rebuild:";
    method.update_command = "git pull --recurse-submodules && make build";
    return method;
  }
  if (UnderPrefix(facts.executable, "/usr/")) {
    if (!facts.pacman_packages.empty()) {
      method.channel = InstallChannel::kPacman;
      method.package = facts.pacman_packages.front();
      method.description = "pacman package " + method.package;
      if (method.package == "mocktail-git") {
        method.update_instructions =
            "mocktail-git builds the development branch. Rebuild it with "
            "your AUR helper, for example:";
        method.update_command = "paru -Syu --devel";
      } else if (method.package == "mocktail-nightly") {
        method.update_instructions =
            "Install the latest nightly package from GitHub:";
        method.update_command = project_url + "/releases/tag/continuous";
      } else {
        method.update_instructions =
            "Update AUR packages with your AUR helper, for example:";
        method.update_command = "paru -Syu";
        if (method.package == "mocktail") {
          method.alternative =
              "The mocktail package compiles every release. mocktail-bin "
              "installs the same release prebuilt, without compiling.";
          method.alternative_command = "paru -S mocktail-bin";
        }
      }
      return method;
    }
    if (!facts.dpkg_packages.empty()) {
      method.channel = InstallChannel::kDpkg;
      method.package = facts.dpkg_packages.front();
      method.description = "Debian package " + method.package;
      if (facts.apt_repository) {
        method.update_instructions = "Update it from Mocktail's APT repository:";
        method.update_command =
            "sudo apt update && sudo apt install --only-upgrade " +
            method.package;
      } else {
        method.update_instructions = "Download the new .deb package:";
        method.update_command = release_url;
      }
      return method;
    }
    if (facts.rpm_database) {
      method.channel = InstallChannel::kRpm;
      method.package = "mocktail";
      method.description = "RPM package";
      if (facts.dnf_repository) {
        method.update_instructions = "Update it from Mocktail's DNF repository:";
        method.update_command = "sudo dnf upgrade --refresh 'mocktail*'";
      } else {
        method.update_instructions = "Download the new .rpm package:";
        method.update_command = release_url;
      }
      return method;
    }
  }
  method.description = "unknown installation";
  method.update_instructions = "Download the new release:";
  method.update_command = release_url;
  return method;
}

}  // namespace mocktail::update
