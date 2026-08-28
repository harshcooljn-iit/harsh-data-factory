#include "flowforge/artifacts/artifact.hpp"

#include <chrono>
#include <system_error>

#include "flowforge/util/sha256.hpp"

namespace flowforge::artifacts {

std::string Artifact::identity() const {
    std::string id = logical_name;
    id += '|';
    id += path.generic_string();
    if (!exists) {
        id += "|absent";
        return id;
    }
    if (checksum) {
        id += "|sha256:" + *checksum;
    } else {
        id += "|size:" + std::to_string(size_bytes);
        id += "|mtime:" + std::to_string(modified_unix_ms);
    }
    return id;
}

Artifact probe_artifact(const std::string& logical_name,
                        const std::filesystem::path& absolute_path,
                        bool want_checksum) {
    Artifact art;
    art.logical_name = logical_name;
    art.path = absolute_path;

    std::error_code ec;
    const auto status = std::filesystem::status(absolute_path, ec);
    if (ec || !std::filesystem::is_regular_file(status)) {
        art.exists = false;
        return art;
    }
    art.exists = true;

    const auto size = std::filesystem::file_size(absolute_path, ec);
    if (!ec) {
        art.size_bytes = static_cast<std::int64_t>(size);
    }

    const auto mtime = std::filesystem::last_write_time(absolute_path, ec);
    if (!ec) {
        // file_clock's epoch is unspecified; file_clock::to_sys() is the
        // standard, portable bridge to system_clock (Unix epoch).
        const auto sys_time = std::chrono::file_clock::to_sys(mtime);
        art.modified_unix_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(sys_time.time_since_epoch())
                .count();
    }

    if (want_checksum) {
        art.checksum = util::sha256_file(absolute_path);
    }
    return art;
}

}  // namespace flowforge::artifacts
