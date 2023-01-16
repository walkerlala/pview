#include <atomic>
#include <memory>
#include <vector>

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/Decl.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Basic/FileManager.h>
#include <clang/Basic/FileSystemOptions.h>
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
#include <clang/Frontend/MultiplexConsumer.h>
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
#include <llvm/Support/CrashRecoveryContext.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/raw_ostream.h>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "PView.h"
#include "siphash.h"

namespace pview {
std::string gen_unified_symbol_resolution(const clang::Decl *d) {
  llvm::SmallString<kUsrLength> ss;
  clang::index::generateUSRForDecl(d, ss);
  return static_cast<std::string>(ss);
}

uint64_t hash_usr(llvm::StringRef s) {
  union {
    uint64_t ret;
    uint8_t out[8];
  };
  // k is an arbitrary key. Don't change it.
  const uint8_t k[16] = {0xd0, 0xe5, 0x4d, 0x61, 0x74, 0x63, 0x68, 0x52,
                         0x61, 0x79, 0xea, 0x70, 0xca, 0x70, 0xf0, 0x0d};
  (void)siphash(reinterpret_cast<const uint8_t *>(s.data()), s.size(), k, out,
                8);
  return ret;
}
}  // namespace pview
