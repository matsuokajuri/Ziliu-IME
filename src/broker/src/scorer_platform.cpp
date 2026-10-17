#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include "scorer_platform.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <psapi.h>
#elif defined(__linux__)
#include <dlfcn.h>
#include <dirent.h>
#include <fcntl.h>
#include <link.h>
#include <openssl/evp.h>
#include <sched.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#error "The optional scorer platform adapter supports Windows and Linux only."
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <limits>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace ziliu::broker::detail {
namespace {

constexpr std::uint64_t kGiB = 1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMinimumAvailableMemory = 8ULL * kGiB;
constexpr std::uint64_t kMaximumProcessMemory = 4ULL * kGiB;
constexpr std::size_t kMaximumRuntimeBytes = 512ULL * 1024ULL * 1024ULL;
constexpr std::size_t kReadChunkBytes = 64U * 1024U;

[[noreturn]] void Fail(const char* message) { throw std::runtime_error(message); }

std::string HexDigest(const std::array<unsigned char, 32>& digest) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(digest.size() * 2U);
  for (const auto byte : digest) {
    result.push_back(digits[byte >> 4U]);
    result.push_back(digits[byte & 15U]);
  }
  return result;
}

class Hash final {
 public:
  Hash() {
#ifdef _WIN32
    if (BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
      Fail("Scorer SHA-256 initialization failed.");
    }
    DWORD object_size = 0;
    DWORD returned = 0;
    DWORD digest_size = 0;
    if (BCryptGetProperty(algorithm_, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size),
                          &returned, 0) < 0 || returned != sizeof(object_size) ||
        object_size == 0 || object_size > 1024U * 1024U ||
        BCryptGetProperty(algorithm_, BCRYPT_HASH_LENGTH,
                          reinterpret_cast<PUCHAR>(&digest_size), sizeof(digest_size),
                          &returned, 0) < 0 || returned != sizeof(digest_size) ||
        digest_size != 32) {
      BCryptCloseAlgorithmProvider(algorithm_, 0);
      algorithm_ = nullptr;
      Fail("Scorer SHA-256 initialization failed.");
    }
    try {
      object_.resize(object_size);
    } catch (...) {
      BCryptCloseAlgorithmProvider(algorithm_, 0);
      algorithm_ = nullptr;
      throw;
    }
    if (BCryptCreateHash(algorithm_, &hash_, object_.data(), object_size,
                         nullptr, 0, 0) < 0) {
      BCryptCloseAlgorithmProvider(algorithm_, 0);
      algorithm_ = nullptr;
      Fail("Scorer SHA-256 initialization failed.");
    }
#else
    hash_ = EVP_MD_CTX_new();
    if (hash_ == nullptr) {
      Fail("Scorer SHA-256 initialization failed.");
    }
    if (EVP_DigestInit_ex(hash_, EVP_sha256(), nullptr) != 1) {
      EVP_MD_CTX_free(hash_);
      hash_ = nullptr;
      Fail("Scorer SHA-256 initialization failed.");
    }
#endif
  }
  ~Hash() {
#ifdef _WIN32
    if (hash_ != nullptr) {
      BCryptDestroyHash(hash_);
    }
    if (algorithm_ != nullptr) {
      BCryptCloseAlgorithmProvider(algorithm_, 0);
    }
#else
    EVP_MD_CTX_free(hash_);
#endif
  }
  Hash(const Hash&) = delete;
  Hash& operator=(const Hash&) = delete;

  void Update(std::string_view bytes) {
#ifdef _WIN32
    while (!bytes.empty()) {
      const auto length = static_cast<ULONG>(
          std::min(bytes.size(), static_cast<std::size_t>(std::numeric_limits<ULONG>::max())));
      if (BCryptHashData(hash_, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
                         length, 0) < 0) {
        Fail("Scorer SHA-256 calculation failed.");
      }
      bytes.remove_prefix(length);
    }
#else
    if (!bytes.empty() && EVP_DigestUpdate(hash_, bytes.data(), bytes.size()) != 1) {
      Fail("Scorer SHA-256 calculation failed.");
    }
#endif
  }

  std::string Finish() {
    std::array<unsigned char, 32> digest{};
#ifdef _WIN32
    if (BCryptFinishHash(hash_, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
      Fail("Scorer SHA-256 calculation failed.");
    }
#else
    unsigned int size = 0;
    if (EVP_DigestFinal_ex(hash_, digest.data(), &size) != 1 || size != digest.size()) {
      Fail("Scorer SHA-256 calculation failed.");
    }
#endif
    return HexDigest(digest);
  }

 private:
#ifdef _WIN32
  BCRYPT_ALG_HANDLE algorithm_ = nullptr;
  BCRYPT_HASH_HANDLE hash_ = nullptr;
  std::vector<unsigned char> object_;
#else
  EVP_MD_CTX* hash_ = nullptr;
#endif
};

#ifdef _WIN32
using NativeHandle = HANDLE;
const NativeHandle kInvalidHandle = INVALID_HANDLE_VALUE;
#else
using NativeHandle = int;
constexpr NativeHandle kInvalidHandle = -1;
#endif

class Handle final {
 public:
  explicit Handle(NativeHandle value = kInvalidHandle) : value_(value) {}
  ~Handle() {
    if (value_ != kInvalidHandle) {
#ifdef _WIN32
      CloseHandle(value_);
#else
      close(value_);
#endif
    }
  }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  Handle(Handle&& other) noexcept : value_(std::exchange(other.value_, kInvalidHandle)) {}
  Handle& operator=(Handle&&) = delete;
  [[nodiscard]] NativeHandle get() const { return value_; }

 private:
  NativeHandle value_;
};

bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right) {
#ifdef _WIN32
  const auto& a = left.native();
  const auto& b = right.native();
  if (a.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      b.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return false;
  }
  return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
                              static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
#else
  return left == right;
#endif
}

std::filesystem::path CanonicalPath(const std::filesystem::path& path) {
  std::error_code error;
  auto result = std::filesystem::canonical(path, error);
  if (error || !result.is_absolute()) {
    Fail("Scorer file path cannot be verified.");
  }
  return result.make_preferred();
}

std::filesystem::path CheckedPath(const std::filesystem::path& path, bool require_canonical) {
  if (path.empty() || path.native().find(std::filesystem::path::value_type{}) !=
                          std::filesystem::path::string_type::npos ||
      (require_canonical && !path.is_absolute())) {
    Fail("Scorer file path is invalid.");
  }
  std::error_code error;
  auto absolute = std::filesystem::absolute(path, error);
  if (error || !absolute.is_absolute()) {
    Fail("Scorer file path is invalid.");
  }
  absolute.make_preferred();
  // Reject dot traversal rather than resolving it across a possible symlink.
  for (const auto& part : absolute.relative_path()) {
    if (part.empty() || part == "." || part == "..") {
      Fail("Scorer file path is not canonical.");
    }
#ifdef _WIN32
    const auto& name = part.native();
    if (name.find(L':') != std::wstring::npos || name.back() == L'.' || name.back() == L' ') {
      Fail("Scorer file path is invalid.");
    }
#endif
  }
#ifdef _WIN32
  const auto root = absolute.root_name().native();
  if (root.size() != 2 || root[1] != L':' ||
      !((root[0] >= L'A' && root[0] <= L'Z') || (root[0] >= L'a' && root[0] <= L'z'))) {
    Fail("Scorer files require a local drive path.");
  }
#endif
  const auto canonical = CanonicalPath(absolute);
  if (!SamePath(canonical, absolute)) {
    Fail("Scorer file path is not canonical.");
  }
  return canonical;
}

#ifdef _WIN32
std::filesystem::path HandlePath(HANDLE handle) {
  std::wstring path(32768, L'\0');
  const DWORD length = GetFinalPathNameByHandleW(handle, path.data(),
                                                static_cast<DWORD>(path.size()),
                                                FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (length == 0 || static_cast<std::size_t>(length) >= path.size()) {
    Fail("Scorer file identity cannot be verified.");
  }
  path.resize(length);
  if (path.starts_with(L"\\\\?\\") && path.size() > 6 && path[5] == L':') {
    path.erase(0, 4);
  }
  return std::filesystem::path(path).make_preferred();
}
#endif

class PinnedFile final {
 public:
  PinnedFile(const std::filesystem::path& path, std::size_t maximum_bytes,
             bool require_canonical = false)
      : path_(CheckedPath(path, require_canonical)) {
    // Keep every parent open. Windows denies directory deletion/renaming;
    // Linux openat + O_NOFOLLOW ensures each traversed component is a directory.
#ifdef _WIN32
    auto current = path_.root_path();
    OpenDirectory(current);
    const auto relative = path_.relative_path();
    for (auto iterator = relative.begin(); iterator != relative.end(); ++iterator) {
      current /= *iterator;
      if (std::next(iterator) != relative.end()) {
        OpenDirectory(current);
      }
    }
    AddHandle(CreateFileW(path_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                      OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (handle() == INVALID_HANDLE_VALUE || GetFileType(handle()) != FILE_TYPE_DISK ||
        !GetFileInformationByHandle(handle(), &identity_) ||
        (identity_.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
        !SamePath(HandlePath(handle()), path_)) {
      Fail("Scorer file is not a verified regular file.");
    }
    size_ = (static_cast<std::uint64_t>(identity_.nFileSizeHigh) << 32U) |
            identity_.nFileSizeLow;
#else
    AddHandle(open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (handles_.back().get() < 0) {
      Fail("Scorer file cannot be opened.");
    }
    const auto relative = path_.relative_path();
    for (auto iterator = relative.begin(); iterator != relative.end(); ++iterator) {
      const bool directory = std::next(iterator) != relative.end();
      const int flags = O_CLOEXEC | O_NOFOLLOW |
                        (directory ? (O_RDONLY | O_DIRECTORY) : O_PATH);
      const int next = openat(handles_.back().get(), iterator->c_str(), flags);
      if (next < 0) {
        Fail("Scorer file cannot be opened without following links.");
      }
      AddHandle(next);
    }
    if (fstat(handle(), &identity_) != 0 || !S_ISREG(identity_.st_mode) || identity_.st_size < 0) {
      Fail("Scorer file is not a verified regular file.");
    }
    // O_PATH classifies the final inode before opening it for I/O, so a FIFO
    // or device cannot cause a blocking or side-effecting ordinary open.
    const auto descriptor = std::string("/proc/self/fd/") + std::to_string(handle());
    AddHandle(open(descriptor.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK));
    if (handle() < 0) {
      Fail("Scorer regular file cannot be opened for reading.");
    }
    size_ = static_cast<std::uint64_t>(identity_.st_size);
    // This is cooperative protection, not a claim to exclude hostile writers.
    if (flock(handle(), LOCK_SH | LOCK_NB) != 0) {
      Fail("Scorer file cannot be pinned for reading.");
    }
#endif
    if (size_ > maximum_bytes || size_ > std::numeric_limits<std::size_t>::max()) {
      Fail("Scorer file exceeds its size limit.");
    }
    CheckStable();
  }

  [[nodiscard]] NativeHandle handle() const { return handles_.back().get(); }
  [[nodiscard]] const std::filesystem::path& path() const { return path_; }

  void CheckStable() const {
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION now{};
    if (!GetFileInformationByHandle(handle(), &now) ||
        now.dwVolumeSerialNumber != identity_.dwVolumeSerialNumber ||
        now.nFileIndexHigh != identity_.nFileIndexHigh || now.nFileIndexLow != identity_.nFileIndexLow ||
        now.nFileSizeHigh != identity_.nFileSizeHigh || now.nFileSizeLow != identity_.nFileSizeLow ||
        now.ftLastWriteTime.dwHighDateTime != identity_.ftLastWriteTime.dwHighDateTime ||
        now.ftLastWriteTime.dwLowDateTime != identity_.ftLastWriteTime.dwLowDateTime ||
        (now.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
        !SamePath(HandlePath(handle()), path_)) {
      Fail("Scorer file changed during verification.");
    }
#else
    struct stat now {};
    if (fstat(handle(), &now) != 0 || !S_ISREG(now.st_mode) ||
        now.st_dev != identity_.st_dev || now.st_ino != identity_.st_ino ||
        now.st_size != identity_.st_size ||
        now.st_mtim.tv_sec != identity_.st_mtim.tv_sec || now.st_mtim.tv_nsec != identity_.st_mtim.tv_nsec ||
        now.st_ctim.tv_sec != identity_.st_ctim.tv_sec || now.st_ctim.tv_nsec != identity_.st_ctim.tv_nsec) {
      Fail("Scorer file changed during verification.");
    }
    const auto descriptor = std::filesystem::path("/proc/self/fd") / std::to_string(handle());
    if (!SamePath(CanonicalPath(descriptor), path_)) {
      Fail("Scorer file path changed during verification.");
    }
#endif
  }

  [[nodiscard]] bool SameIdentity(const PinnedFile& other) const {
#ifdef _WIN32
    return identity_.dwVolumeSerialNumber == other.identity_.dwVolumeSerialNumber &&
           identity_.nFileIndexHigh == other.identity_.nFileIndexHigh &&
           identity_.nFileIndexLow == other.identity_.nFileIndexLow;
#else
    return identity_.st_dev == other.identity_.st_dev && identity_.st_ino == other.identity_.st_ino;
#endif
  }

  template <typename Consumer>
  void Read(Consumer consumer) const {
    const std::lock_guard lock(read_mutex_);
    CheckStable();
#ifdef _WIN32
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(handle(), zero, nullptr, FILE_BEGIN)) {
      Fail("Scorer file cannot be read.");
    }
#endif
    std::array<char, kReadChunkBytes> buffer{};
    std::uint64_t offset = 0;
    while (offset < size_) {
      const auto requested = static_cast<std::size_t>(
          std::min<std::uint64_t>(size_ - offset, buffer.size()));
#ifdef _WIN32
      DWORD count = 0;
      if (!ReadFile(handle(), buffer.data(), static_cast<DWORD>(requested), &count, nullptr) ||
          count == 0 || count > requested) {
        Fail("Scorer file cannot be read completely.");
      }
      const auto received = static_cast<std::size_t>(count);
#else
      const auto count = pread(handle(), buffer.data(), requested, static_cast<off_t>(offset));
      if (count < 0 && errno == EINTR) {
        continue;
      }
      if (count <= 0 || static_cast<std::uint64_t>(count) > requested) {
        Fail("Scorer file cannot be read completely.");
      }
      const auto received = static_cast<std::size_t>(count);
#endif
      consumer(std::string_view(buffer.data(), received));
      offset += received;
    }
    CheckStable();
  }

  [[nodiscard]] std::string Digest() const {
    Hash hash;
    Read([&hash](std::string_view bytes) { hash.Update(bytes); });
    return hash.Finish();
  }

  [[nodiscard]] std::string Bytes() const {
    std::string bytes;
    if (size_ > bytes.max_size()) {
      Fail("Scorer file exceeds its size limit.");
    }
    bytes.reserve(static_cast<std::size_t>(size_));
    Read([&bytes](std::string_view part) { bytes.append(part); });
    return bytes;
  }

 private:
  void AddHandle(NativeHandle value) {
    Handle handle(value);
    handles_.push_back(std::move(handle));
  }
#ifdef _WIN32
  void OpenDirectory(const std::filesystem::path& path) {
    AddHandle(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                                      nullptr));
    BY_HANDLE_FILE_INFORMATION information{};
    if (handles_.back().get() == INVALID_HANDLE_VALUE ||
        !GetFileInformationByHandle(handles_.back().get(), &information) ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
      Fail("Scorer file parent is not a verified directory.");
    }
  }
  BY_HANDLE_FILE_INFORMATION identity_{};
#else
  struct stat identity_ {};
#endif
  std::filesystem::path path_;
  std::vector<Handle> handles_;
  std::uint64_t size_ = 0;
  mutable std::mutex read_mutex_;
};

class Module final {
 public:
#ifdef _WIN32
  HMODULE value = nullptr;
#else
  void* value = nullptr;
#endif
  ~Module() {
    if (value != nullptr) {
#ifdef _WIN32
      FreeLibrary(value);
#else
      dlclose(value);
#endif
    }
  }
  Module() = default;
  Module(const Module&) = delete;
  Module& operator=(const Module&) = delete;
};

std::string ResourceMetrics(std::uint64_t available, std::uint64_t resident,
                            std::uint64_t cap, unsigned int cpus) {
  return " available_physical_bytes=" + std::to_string(available) +
         " process_resident_bytes=" + std::to_string(resident) +
         " enforced_process_limit_bytes=" + std::to_string(cap) +
         " affinity_cpu_count=" + std::to_string(cpus) + ".";
}

#ifndef _WIN32
std::uint64_t LinuxAvailableMemory() {
  std::ifstream stream("/proc/meminfo");
  std::string key;
  std::string line;
  while (stream >> key) {
    if (key == "MemAvailable:") {
      std::uint64_t kib = 0;
      std::string unit;
      if (!(stream >> kib >> unit) || unit != "kB" ||
          kib > std::numeric_limits<std::uint64_t>::max() / 1024U) {
        break;
      }
      return kib * 1024U;
    }
    std::getline(stream, line);
  }
  Fail("Scorer available physical memory cannot be verified.");
}

std::uint64_t LinuxResidentMemory() {
  std::ifstream stream("/proc/self/statm");
  std::uint64_t virtual_pages = 0;
  std::uint64_t resident_pages = 0;
  const long page_size = sysconf(_SC_PAGESIZE);
  if (!(stream >> virtual_pages >> resident_pages) || page_size <= 0 ||
      resident_pages > std::numeric_limits<std::uint64_t>::max() /
                           static_cast<std::uint64_t>(page_size)) {
    Fail("Scorer process resident memory cannot be verified.");
  }
  return resident_pages * static_cast<std::uint64_t>(page_size);
}

unsigned int LinuxProcessAffinity() {
  struct DirectoryCloser {
    void operator()(DIR* directory) const { closedir(directory); }
  };
  std::unique_ptr<DIR, DirectoryCloser> directory(opendir("/proc/self/task"));
  if (!directory) {
    Fail("Scorer process affinity cannot be verified.");
  }
  cpu_set_t combined;
  CPU_ZERO(&combined);
  unsigned int examined = 0;
  for (;;) {
    errno = 0;
    const dirent* entry = readdir(directory.get());
    if (entry == nullptr) {
      if (errno != 0) {
        Fail("Scorer process affinity cannot be verified.");
      }
      break;
    }
    const std::string_view name(entry->d_name);
    if (name == "." || name == "..") {
      continue;
    }
    pid_t thread = 0;
    const auto [end, error] = std::from_chars(name.data(), name.data() + name.size(), thread);
    if (error != std::errc{} || end != name.data() + name.size() || thread <= 0 ||
        ++examined > 4096) {
      Fail("Scorer process affinity cannot be verified.");
    }
    cpu_set_t affinity;
    CPU_ZERO(&affinity);
    if (sched_getaffinity(thread, sizeof(affinity), &affinity) != 0) {
      if (errno == ESRCH) {
        continue;
      }
      Fail("Scorer process affinity cannot be verified.");
    }
    CPU_OR(&combined, &combined, &affinity);
  }
  const int count = CPU_COUNT(&combined);
  if (count <= 0) {
    Fail("Scorer process affinity cannot be verified.");
  }
  return static_cast<unsigned int>(count);
}
#endif

}  // namespace

std::string Sha256(std::string_view bytes) {
  Hash hash;
  hash.Update(bytes);
  return hash.Finish();
}

std::string ReadBoundedFile(const std::filesystem::path& path, std::size_t maximum_bytes) {
  return PinnedFile(path, maximum_bytes).Bytes();
}

class RuntimeLibrary::Impl final {
 public:
  Impl(const std::filesystem::path& path, std::string_view expected_sha256)
      : file_(path, kMaximumRuntimeBytes, true), expected_sha256_(expected_sha256) {
    if (expected_sha256_.size() != 64 ||
        !std::all_of(expected_sha256_.begin(), expected_sha256_.end(), [](char character) {
          return (character >= '0' && character <= '9') ||
                 (character >= 'a' && character <= 'f');
        }) || file_.Digest() != expected_sha256_) {
      Fail("Scorer runtime digest is not approved.");
    }
    // Loading native code is not a security sandbox. Only independently
    // allowlisted official runtime bytes belong here. The caller must use a
    // trusted installation directory and a fresh dedicated worker. An attacker
    // already able to change this process or its loader environment, privileged
    // mappings, or (on POSIX) the pinned inode in place is outside this boundary.
#ifdef _WIN32
    // Deny-write/delete handles remain alive through FreeLibrary; parent handles
    // also prevent ordinary path replacement. Do not alter global DLL policy.
    module_.value = LoadLibraryExW(file_.path().c_str(), nullptr,
                                   LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                       LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
    // Loading the pinned descriptor prevents a pathname swap between hashing
    // and dlopen from selecting a different inode. This intentionally does not
    // support $ORIGIN-relative private dependencies; only the approved runtime
    // plus trusted system dependencies may be used. flock is only cooperative:
    // non-cooperating same-user in-place writes require installation isolation.
    descriptor_path_ = "/proc/self/fd/" + std::to_string(file_.handle());
    module_.value = dlopen(descriptor_path_.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    if (module_.value == nullptr) {
      Fail("Scorer runtime could not be loaded.");
    }
    VerifyLoadedFile();
  }

  void* Symbol(const char* name) {
    if (name == nullptr || *name == '\0') {
      Fail("Scorer runtime symbol is invalid.");
    }
    VerifyLoadedFile();
#ifdef _WIN32
    const FARPROC symbol = GetProcAddress(module_.value, name);
    if (symbol == nullptr) {
      Fail("Scorer runtime symbol is unavailable.");
    }
    void* address = std::bit_cast<void*>(symbol);
    HMODULE origin = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCWSTR>(address), &origin) || origin != module_.value) {
      Fail("Scorer runtime symbol provenance is invalid.");
    }
#else
    dlerror();
    void* address = dlsym(module_.value, name);
    if (address == nullptr || dlerror() != nullptr) {
      Fail("Scorer runtime symbol is unavailable.");
    }
    Dl_info information{};
    if (dladdr(address, &information) == 0 || information.dli_fname == nullptr ||
        !SamePath(CanonicalPath(information.dli_fname), file_.path())) {
      Fail("Scorer runtime symbol provenance is invalid.");
    }
#endif
    VerifyLoadedFile();
    return address;
  }

  [[nodiscard]] std::filesystem::path loaded_path() const { return file_.path(); }

 private:
  void VerifyLoadedFile() const {
#ifdef _WIN32
    std::wstring module_path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(module_.value, module_path.data(),
                                           static_cast<DWORD>(module_path.size()));
    if (length == 0 || static_cast<std::size_t>(length) >= module_path.size()) {
      Fail("Scorer loaded runtime path cannot be verified.");
    }
    module_path.resize(length);
    const auto actual = CheckedPath(std::filesystem::path(module_path), true);
#else
    link_map* map = nullptr;
    if (dlinfo(module_.value, RTLD_DI_LINKMAP, &map) != 0 || map == nullptr ||
        map->l_name == nullptr || *map->l_name == '\0') {
      Fail("Scorer loaded runtime path cannot be verified.");
    }
    const auto actual = CanonicalPath(map->l_name);
#endif
    if (!SamePath(actual, file_.path())) {
      Fail("Scorer loaded runtime path is not approved.");
    }
    const PinnedFile current(actual, kMaximumRuntimeBytes, true);
    if (!file_.SameIdentity(current) || current.Digest() != expected_sha256_ ||
        file_.Digest() != expected_sha256_) {
      Fail("Scorer loaded runtime bytes are not approved.");
    }
  }

  PinnedFile file_;
  std::string expected_sha256_;
#ifndef _WIN32
  std::string descriptor_path_;
#endif
  Module module_;
};

RuntimeLibrary::RuntimeLibrary(const std::filesystem::path& path, std::string_view expected_sha256)
    : impl_(std::make_unique<Impl>(path, expected_sha256)) {}
RuntimeLibrary::~RuntimeLibrary() = default;
void* RuntimeLibrary::Symbol(const char* name) { return impl_->Symbol(name); }
std::filesystem::path RuntimeLibrary::loaded_path() const { return impl_->loaded_path(); }

void CheckWorkerResources() {
#ifdef _WIN32
  MEMORYSTATUSEX memory{};
  memory.dwLength = static_cast<DWORD>(sizeof(memory));
  PROCESS_MEMORY_COUNTERS_EX process_memory{};
  process_memory.cb = static_cast<DWORD>(sizeof(process_memory));
  if (!GlobalMemoryStatusEx(&memory) ||
      !GetProcessMemoryInfo(GetCurrentProcess(),
                            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&process_memory),
                            static_cast<DWORD>(sizeof(process_memory)))) {
    Fail("Scorer physical or process working-set memory cannot be verified.");
  }
  BOOL in_job = FALSE;
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  if (!IsProcessInJob(GetCurrentProcess(), nullptr, &in_job) || !in_job ||
      !QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &limits,
                                  static_cast<DWORD>(sizeof(limits)), nullptr) ||
      (limits.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_PROCESS_MEMORY) == 0) {
    throw std::runtime_error("Scorer requires an existing verifiable job process-memory cap."
                             " available_physical_bytes=" + std::to_string(memory.ullAvailPhys) +
                             " process_working_set_bytes=" +
                             std::to_string(process_memory.WorkingSetSize) +
                             " process_private_commit_bytes=" +
                             std::to_string(process_memory.PrivateUsage) + ".");
  }
  // GetProcessAffinityMask cannot prove a total cap for a multigroup process.
  // Require exactly one group and fail closed when Windows cannot prove that.
  USHORT group_count = 1;
  USHORT group = 0;
  DWORD_PTR process_mask = 0;
  DWORD_PTR system_mask = 0;
  if (!GetProcessGroupAffinity(GetCurrentProcess(), &group_count, &group) || group_count != 1 ||
      !GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask) ||
      process_mask == 0 || (process_mask & ~system_mask) != 0) {
    Fail("Scorer existing process affinity cannot be verified.");
  }
  const auto cpus = static_cast<unsigned int>(std::popcount(process_mask));
  const auto cap = static_cast<std::uint64_t>(limits.ProcessMemoryLimit);
  const auto available = static_cast<std::uint64_t>(memory.ullAvailPhys);
  const auto resident = static_cast<std::uint64_t>(process_memory.WorkingSetSize);
  if (cap == 0 || cap > kMaximumProcessMemory || cpus > 2 ||
      available < kMinimumAvailableMemory || process_memory.PrivateUsage > cap ||
      resident > kMaximumProcessMemory) {
    throw std::runtime_error("Scorer resource admission rejected (Windows job process-commit cap)." +
                             ResourceMetrics(available, resident, cap, cpus) +
                             " process_private_commit_bytes=" +
                             std::to_string(process_memory.PrivateUsage) + ".");
  }
#else
  const auto available = LinuxAvailableMemory();
  const auto resident = LinuxResidentMemory();
  const auto cpus = LinuxProcessAffinity();
  struct rlimit limit {};
  if (getrlimit(RLIMIT_AS, &limit) != 0) {
    Fail("Scorer existing address-space limit cannot be verified.");
  }
  const auto cap = static_cast<std::uint64_t>(limit.rlim_cur);
  if (limit.rlim_cur == RLIM_INFINITY || limit.rlim_max == RLIM_INFINITY ||
      static_cast<std::uint64_t>(limit.rlim_max) > kMaximumProcessMemory ||
      cap == 0 || cap > kMaximumProcessMemory ||
      cpus > 2 || available < kMinimumAvailableMemory || resident > kMaximumProcessMemory) {
    throw std::runtime_error("Scorer resource admission rejected (Linux RLIMIT_AS address-space cap)." +
                             ResourceMetrics(available, resident, cap, cpus));
  }
#endif
}

}  // namespace ziliu::broker::detail
