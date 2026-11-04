//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 T. Cully
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "machine_id.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>

#if defined(__linux__)
#include <sys/random.h>
#elif defined(__APPLE__)
#include <cstdlib>
#endif

#include "util.h"

namespace rpiledconfig::machine_id {

MachineId MachineId::from_value(std::uint64_t value) {
  MachineId id;
  for (int i = static_cast<int>(id_length) - 1; i >= 0; --i) {
    id.bytes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value & 0xFF);
    value >>= 8;
  }
  return id;
}

std::uint64_t MachineId::value() const {
  std::uint64_t value = 0;
  for (const auto byte : bytes) value = (value << 8) | byte;
  return value;
}

types::Bytes MachineId::id_bytes() const { return types::Bytes(bytes.begin(), bytes.end()); }

std::string MachineId::to_hex() const { return util::to_hex(bytes.data(), bytes.size()); }

std::optional<MachineId> MachineId::from_hex(const std::string& text) {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return std::nullopt;
  const auto last = text.find_last_not_of(" \t\r\n");

  const auto bytes = util::from_hex(text.substr(first, last - first + 1));
  if (!bytes || bytes->size() != id_length) return std::nullopt;

  MachineId id;
  std::copy(bytes->begin(), bytes->end(), id.bytes.begin());
  return id;
}

std::filesystem::path path_in(const std::filesystem::path& state_dir) { return state_dir / file_name; }

types::Result<MachineId> load(const std::filesystem::path& state_dir) {
  const auto path = path_in(state_dir);

  std::ifstream in(path);
  if (!in) return types::Result<MachineId>::failure("cannot read " + path.string() + ": " + std::strerror(errno));

  std::stringstream buffer;
  buffer << in.rdbuf();

  const auto id = MachineId::from_hex(buffer.str());
  if (!id) return types::Result<MachineId>::failure(path.string() + " does not hold 16 hex characters (an id from frame version 3 or 4 has 12 or 6: rpiledconfig --regenerate-id replaces it)");

  return types::Result<MachineId>::success(*id);
}

types::Result<MachineId> random_id() {
  MachineId id;

#if defined(__linux__)
  std::size_t have = 0;
  while (have < id.bytes.size()) {
    const auto got = ::getrandom(id.bytes.data() + have, id.bytes.size() - have, 0);
    if (got < 0) {
      if (errno == EINTR) continue;
      return types::Result<MachineId>::failure(std::string("getrandom: ") + std::strerror(errno));
    }
    have += static_cast<std::size_t>(got);
  }
#elif defined(__APPLE__)
  arc4random_buf(id.bytes.data(), id.bytes.size());
#else
  std::ifstream urandom("/dev/urandom", std::ios::binary);
  if (!urandom.read(reinterpret_cast<char*>(id.bytes.data()), static_cast<std::streamsize>(id.bytes.size()))) {
    return types::Result<MachineId>::failure("cannot read /dev/urandom");
  }
#endif

  return types::Result<MachineId>::success(id);
}

types::Result<MachineId> generate(const std::filesystem::path& state_dir) {
  std::error_code ec;
  std::filesystem::create_directories(state_dir, ec);
  if (ec) return types::Result<MachineId>::failure("cannot make " + state_dir.string() + ": " + ec.message());

  auto id = random_id();
  if (!id) return id;

  const auto path = path_in(state_dir);
  auto temporary = path;
  temporary += ".tmp";
  const std::string text = id.value.to_hex() + "\n";

  const auto fail = [&](const std::string& what) {
    const std::string reason = std::strerror(errno);
    ::unlink(temporary.c_str());
    return types::Result<MachineId>::failure(what + ": " + reason);
  };

  // A temporary file, synced and renamed into place, so a power cut leaves the old file or
  // the new one and never an empty one: a Pi lost power between create and write once, and
  // an empty machine-id stopped every start after it. Opened with the mode rather than
  // chmod'ed after, so the file is never readable by anyone else, not even for an instant.
  const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  if (fd < 0) return fail("cannot write " + temporary.string());

  std::size_t written = 0;
  while (written < text.size()) {
    const auto n = ::write(fd, text.data() + written, text.size() - written);
    if (n < 0) {
      if (errno == EINTR) continue;
      const auto failure = fail("cannot write " + temporary.string());
      ::close(fd);
      return failure;
    }
    written += static_cast<std::size_t>(n);
  }

  // The mode again for a leftover temporary file that was there with another one.
  ::fchmod(fd, 0600);

  // The state directory's owner, who is the service's user: `rpiledconfig --regenerate-id`
  // is run as root, and a 0600 file left owned by root stopped every start after it.
  struct stat dir{};
  if (::stat(state_dir.c_str(), &dir) == 0 && (dir.st_uid != ::geteuid() || dir.st_gid != ::getegid())) {
    if (::fchown(fd, dir.st_uid, dir.st_gid) != 0) {
      const auto failure = fail("cannot give " + temporary.string() + " to the owner of " + state_dir.string());
      ::close(fd);
      return failure;
    }
  }

  if (::fsync(fd) != 0) {
    const auto failure = fail("cannot sync " + temporary.string());
    ::close(fd);
    return failure;
  }
  ::close(fd);

  if (::rename(temporary.c_str(), path.c_str()) != 0) return fail("cannot rename " + temporary.string() + " to " + path.string());

  // The rename is only durable once the directory is: without this a power cut could
  // bring back the old entry.
  const int dir_fd = ::open(state_dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (dir_fd >= 0) {
    ::fsync(dir_fd);
    ::close(dir_fd);
  }

  return id;
}

types::Result<MachineId> load_or_generate(const std::filesystem::path& state_dir) {
  const auto path = path_in(state_dir);

  // An empty file is a write that never finished (docs/protocol.md, The machine ID): there
  // is no ID in it to keep, so it is the same as no file. Anything else is loaded, and
  // refused if it is not an ID, because that is not something to overwrite unasked.
  std::error_code ec;
  if (std::filesystem::exists(path, ec)) {
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size != 0) return load(state_dir);
  }

  return generate(state_dir);
}

}  // namespace rpiledconfig::machine_id
