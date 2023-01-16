#include <optional>
#include <string>

namespace pview {
/** return true if file @file is writable */
bool check_file_writable(const std::string file);

/** return true if directory @dir is writable */
bool check_dir_writable(const std::string &dir);

/** return true if @port is in use by some process */
bool check_port_in_use(int port);

/** return true if @file has exec permission */
bool check_file_executable(const std::string &file);

/** Resolve all hard/soft link to get real path */
std::string real_path(const std::string &path);

/** write @content to @file using C API */
void write2file(const std::string &filename, const std::string &content);

/** read from file using C API */
std::optional<std::string> read_content(const std::string &filename);

/** Get the modified time of a file, or -1 if file not exists */
int64_t get_file_mtime(const std::string &filepath);
}  // namespace pview
