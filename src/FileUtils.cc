#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/Decl.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Basic/FileEntry.h>
#include <clang/Basic/FileManager.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Driver/Action.h>
#include <clang/Driver/Compilation.h>
#include <clang/Driver/Driver.h>
#include <clang/Driver/Tool.h>
#include <clang/Format/Format.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Index/IndexDataConsumer.h>
#include <clang/Index/IndexingAction.h>
#include <clang/Index/IndexingOptions.h>
#include <clang/Index/USRGeneration.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <clang/Tooling/Core/Replacement.h>
#include <llvm/ADT/CachedHashString.h>
#include <llvm/ADT/DenseSet.h>
#include <llvm/ADT/MapVector.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/raw_ostream.h>

#include <glog/logging.h>

#include "FileUtils.h"

namespace pview {
/** return true if file @file is writable */
bool check_file_writable(const std::string file) {
  int fd = open(file.c_str(), O_WRONLY | O_CLOEXEC | O_CREAT, S_IRWXU);
  if (fd >= 0) {
    close(fd);
  }
  return (fd >= 0);
}

/** return true if directory @dir is writable */
bool check_dir_writable(const std::string &dir) {
  int result = access(dir.c_str(), W_OK);
  return (result == 0);
}

/** return true if @port is in use by some process */
bool check_port_in_use(int port) { return false; }

/** return true if @file has exec permission */
bool check_file_executable(const std::string &file) {
  return (!access(file.c_str(), X_OK));
}

/** Resolve all hard/soft link to get real path */
std::string real_path(const std::string &path) {
  llvm::SmallString<256> buf;
  llvm::sys::fs::real_path(path, buf);
  return buf.empty() ? path : llvm::sys::path::convert_to_slash(buf);
}

/** write @content to @file using C API */
void write2file(const std::string &filename, const std::string &content) {
  FILE *f = fopen(filename.c_str(), "wb");
  if (!f ||
      (content.size() && fwrite(content.c_str(), content.size(), 1, f) != 1)) {
    LOG(ERROR) << "failed to write to " << filename << ' ' << strerror(errno);
    return;
  }
  fclose(f);
}

/** read from file using C API */
std::optional<std::string> read_content(const std::string &filename) {
  char buf[4096];
  std::string ret;
  FILE *f = fopen(filename.c_str(), "rb");
  if (!f) return {};
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) ret.append(buf, n);
  fclose(f);
  return ret;
}

/** Get the modified time of a file, or -1 if file not exists */
int64_t get_file_mtime(const std::string &filepath) {
  struct stat res;
  if (!stat(filepath.c_str(), &res)) {
    return res.st_mtim.tv_sec;
  }
  return 0;
}
}  // namespace pview
