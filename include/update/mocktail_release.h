#ifndef MOCKTAIL_UPDATE_MOCKTAIL_RELEASE_H_
#define MOCKTAIL_UPDATE_MOCKTAIL_RELEASE_H_

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "update/http_download.h"
#include "update/install_method.h"

namespace mocktail::update {

// Mocktail's own releases, as opposed to the Roblox payload. Mocktail never
// replaces its own files: the Flatpak, the package manager, or the user owns
// them. This only finds out whether a newer release exists and whether that
// release can run a Roblox version this build had to reject.

struct ReleaseVersion {
  std::uint32_t major = 0;
  std::uint32_t minor = 0;
  std::uint32_t patch = 0;
};

// Accepts "1.0.4" and "v1.0.4"; "1.0" means "1.0.0". Anything else is empty.
std::optional<ReleaseVersion> ParseReleaseVersion(std::string_view text);
int CompareReleaseVersions(const ReleaseVersion& left,
                           const ReleaseVersion& right);

// OWNER/REPO with GitHub's permitted characters.
bool IsValidRepositorySlug(std::string_view repository);

struct MocktailRelease {
  // Git tag as published, and the same version without a "v" prefix.
  std::string tag;
  std::string version;
  std::string url;
  // Roblox version codes the release's own compatibility catalog lists as
  // supported. Only meaningful when `catalog_known` is true.
  std::vector<std::uint64_t> supported_roblox_codes;
  bool catalog_known = false;
};

struct MocktailReleaseResult {
  MocktailRelease release;
  std::string error;

  explicit operator bool() const { return error.empty(); }
};

using HttpFetcher = std::function<HttpBytesResult(const HttpTransferRequest&)>;

// Parses GitHub's "latest release" document for `repository`.
MocktailReleaseResult ParseLatestReleaseDocument(std::string_view document,
                                                 std::string_view repository);

MocktailReleaseResult FetchLatestMocktailRelease(std::string_view repository,
                                                 const HttpFetcher& fetch);

struct MocktailReleaseCheckOptions {
  std::string repository;
  std::filesystem::path state_file;
  std::time_t now = 0;
  // GitHub allows 60 anonymous API requests an hour per address; one check
  // every few hours keeps launches fast and far below that.
  std::time_t refresh_seconds = 12 * 60 * 60;
  std::time_t retry_seconds = 60 * 60;
  bool force_refresh = false;
  HttpFetcher fetch;
};

struct MocktailReleaseCheck {
  std::optional<MocktailRelease> latest;
  bool refreshed = false;
  // Last update notice shown to the user, so each one appears only once.
  std::string notified_key;
  std::string error;
};

MocktailReleaseCheck CheckMocktailRelease(
    const MocktailReleaseCheckOptions& options);

bool RecordNotifiedKey(const std::filesystem::path& state_file,
                       std::string_view key, std::string* error);

struct RobloxUpdateState {
  std::string active_version_name;
  std::uint64_t active_version_code = 0;
  std::string latest_version_name;
  std::uint64_t latest_version_code = 0;
  // The provider's latest Roblox failed compatibility or probation with this
  // runtime. A download failure does not count: it resolves on its own.
  bool latest_rejected = false;
};

struct UpdateNotice {
  // Identifies the situation; the same key is not shown twice.
  std::string key;
  std::string heading;
  std::string body;
  std::string command;
  std::string alternative;
  std::string alternative_command;

  bool empty() const { return key.empty(); }
};

UpdateNotice ComposeUpdateNotice(std::string_view installed_version,
                                 const std::optional<MocktailRelease>& latest,
                                 const RobloxUpdateState& roblox,
                                 const InstallMethod& install);

}  // namespace mocktail::update

#endif  // MOCKTAIL_UPDATE_MOCKTAIL_RELEASE_H_
