#ifndef MOCKTAIL_UPDATE_INSTALL_METHOD_H_
#define MOCKTAIL_UPDATE_INSTALL_METHOD_H_

#include <filesystem>
#include <string>
#include <vector>

namespace mocktail::update {

// How this copy of Mocktail reached the machine. It decides who is allowed
// to replace Mocktail's own files: always the package manager, Flatpak, or
// the user, never Mocktail itself, so the updater only names the command.
enum class InstallChannel {
  kUnknown,
  kFlatpak,
  kAppImage,
  kNix,
  kPacman,
  kDpkg,
  kRpm,
  kSourceBuild,
};

// Everything the classification reads from the host. Gathered separately so
// tests can describe a machine without touching the real filesystem.
struct InstallFacts {
  bool in_flatpak = false;
  std::string flatpak_app_id;
  std::string appimage_path;
  std::filesystem::path executable;
  bool build_tree = false;
  // Installed package names that start with "mocktail".
  std::vector<std::string> pacman_packages;
  std::vector<std::string> dpkg_packages;
  bool rpm_database = false;
  bool apt_repository = false;
  bool dnf_repository = false;
  // Host os-release; inside Flatpak it is read from /run/host/os-release.
  std::string os_id;
  std::vector<std::string> os_id_like;
  std::string os_version_id;
  std::string os_variant_id;
  std::string os_pretty_name;
};

struct InstallMethod {
  InstallChannel channel = InstallChannel::kUnknown;
  // Package name or application ID the update command operates on.
  std::string package;
  // Short channel name for logs and the status command.
  std::string description;
  // Sentence telling the user how updates reach this installation.
  std::string update_instructions;
  // Copyable command or URL; empty when there is nothing to copy.
  std::string update_command;
  // Optional lighter installation for the same machine, for example the
  // distribution's native package instead of the Flatpak. Informational only.
  std::string alternative;
  std::string alternative_command;
};

// `root` is "/" in production; tests point it at a fake tree.
InstallFacts GatherInstallFacts(const std::filesystem::path& root,
                                const std::filesystem::path& executable,
                                const std::string& appimage_path);

// `project_url` is https://github.com/OWNER/REPO and `release_url` the page
// of the release the user should move to.
InstallMethod DescribeInstallMethod(const InstallFacts& facts,
                                    const std::string& project_url,
                                    const std::string& release_url);

const char* InstallChannelName(InstallChannel channel);

}  // namespace mocktail::update

#endif  // MOCKTAIL_UPDATE_INSTALL_METHOD_H_
