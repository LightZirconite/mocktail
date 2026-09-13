#ifndef MOCKTAIL_UPDATE_INSTALL_METHOD_H_
#define MOCKTAIL_UPDATE_INSTALL_METHOD_H_

#include <filesystem>
#include <string>
#include <vector>

namespace mocktail::update {

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

struct InstallFacts {
  bool in_flatpak = false;
  std::string flatpak_app_id;
  std::string appimage_path;
  std::filesystem::path executable;
  bool build_tree = false;
  std::vector<std::string> pacman_packages;
  std::vector<std::string> dpkg_packages;
  bool rpm_database = false;
  bool apt_repository = false;
  bool dnf_repository = false;
  std::string os_id;
  std::vector<std::string> os_id_like;
  std::string os_version_id;
  std::string os_variant_id;
  std::string os_pretty_name;
};

struct InstallMethod {
  InstallChannel channel = InstallChannel::kUnknown;
  std::string package;
  std::string description;
  std::string update_instructions;
  std::string update_command;
  std::string alternative;
  std::string alternative_command;
};

InstallFacts GatherInstallFacts(const std::filesystem::path& root,
                                const std::filesystem::path& executable,
                                const std::string& appimage_path);

InstallMethod DescribeInstallMethod(const InstallFacts& facts,
                                    const std::string& project_url,
                                    const std::string& release_url);

const char* InstallChannelName(InstallChannel channel);

}  // namespace mocktail::update

#endif  // MOCKTAIL_UPDATE_INSTALL_METHOD_H_
