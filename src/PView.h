#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/Decl.h>
#include <clang/AST/RecursiveASTVisitor.h>
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
#include <clang/Frontend/MultiplexConsumer.h>
#include <clang/Index/IndexDataConsumer.h>
#include <clang/Index/IndexingAction.h>
#include <clang/Index/IndexingOptions.h>

#include <clang/Index/USRGeneration.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Core/Replacement.h>
#include <llvm/ADT/CachedHashString.h>
#include <llvm/ADT/DenseSet.h>
#include <llvm/ADT/MapVector.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/Support/CrashRecoveryContext.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/raw_ostream.h>

#include "Common.h"

namespace pview {
class ClassDef;
class FuncDef;
class FuncCall;

constexpr size_t kUsrLength = 256;
constexpr uint64_t kInvalidLine = UINT64_MAX;
constexpr uint64_t kInvalidColumn = UINT64_MAX;

/**
 * Limit maximum filepath len for each indexed file.
 * The maximum filepath len on Linux is usually 4096, but it is too long for
 * mysql, so we truncate those lenghty path to "FILE_PATH_TOO_LONG".
 *
 * Note that all indexed file under the project root is truncated to be
 * related to the project root. Therefore it is usually not that long for
 * most file.
 */
constexpr size_t kMaxFilePathLen = 2048;
constexpr const char *kTooLongFilePath = "FILE_PATH_TOO_LONG";
constexpr int64_t kInvalidFileId = -1;

/******************************************************************************
 * Corresponds to clang::SourceLocation
 *
 ******************************************************************************/
struct Location {
  std::string file;
  int64_t file_id;
  uint64_t line;
  uint64_t column;
};

/******************************************************************************
 * Corresponds to clang::SourceRange
 *
 ******************************************************************************/
struct Range {
  Location start;
  Location end;
};

/******************************************************************************
 * DeclInfo represent every function/method/class/objec symbol that we met
 * when indexing the project.
 *
 ******************************************************************************/
struct DeclInfo {
  /**
   * In the LLVM world, "USR" stands for "Unified Symbol Resolution" values.
   * USRs are strings that provide an unambiguous reference to a symbol.
   */
  std::string usr;
  /**
   * hash value of @usr
   */
  uint64_t usr_hash;
  /**
   * Name without any namespace/class qualifier.
   * For example, for any symbol of NameSpace1::Class2::Func2(), its short name
   * is Func2
   */
  std::string short_name;
  /**
   * Fully qualifier for a symbol.
   * In theory this could be to uniquely identifier a function/method/class
   * definition node.
   */
  std::string qualified;
  /**
   * Hash value of @qualified
   */
  uint64_t qualified_hash;
  /**
   * Symbol location
   */
  Location source_location;
  /**
   * Range of definition body. For function/method/class definition.
   */
  Range source_range;
};

/******************************************************************************
 * Function definition, including class method, normal function, and function
 * like macro.
 *
 * For function analysis (caller, callee, callpath, etc), we handle
 * function-like macro as function.
 ******************************************************************************/
class FuncDef {
 public:
  /**
   * If it is a cxx method, then by the time we met this function definition,
   * we must have already seen definition of its class, otherwise it would
   * not compile successfully, i.e., if @cls is nullptr, this FuncDef is
   * ill-formed.
   */
  FuncDef(int64_t id, const DeclInfo &info, bool is_static, bool is_virtual,
          const std::shared_ptr<ClassDef> &cls)
      : id_(id),
        info_(info),
        is_static_(is_static),
        is_virtual_(is_virtual),
        cls_(cls) {}
  virtual ~FuncDef() = default;

  const DeclInfo &get_info() const { return info_; }
  bool is_static() const { return is_static_; }
  bool is_virtual() const { return is_virtual_; }
  virtual bool is_macro() const { return false; }
  auto get_class() { return cls_.lock(); }

  int64_t get_id() const { return id_; }

 protected:
  const int64_t id_;
  const DeclInfo info_;
  bool is_static_;
  bool is_virtual_;

  /* if it is cxx method, cls_ != nullptr */
  std::weak_ptr<ClassDef> cls_;
};
using FuncDefPtr = std::shared_ptr<FuncDef>;

/******************************************************************************
 * Represent a "function-like" macro, i.e., C/C++ macro which accept arguments
 *
 ******************************************************************************/
class FuncLikeMacroDef : public FuncDef {
 public:
  FuncLikeMacroDef(int64_t id, const DeclInfo &info)
      : FuncDef(id, info, false, false, nullptr) {}
  ~FuncLikeMacroDef() = default;

  bool is_macro() const override { return true; }
};

/******************************************************************************
 * Function call, the symbol that call a function or cxx method or macro
 *
 ******************************************************************************/
class FuncCall {
 public:
  FuncCall(int64_t id, const DeclInfo &info) : id_(id), info_(info) {}
  virtual ~FuncCall() = default;

  const DeclInfo &get_info() const { return info_; }

  virtual bool is_macro() const { return false; }

  void set_caller_info(uint64_t caller_usr_hash,
                       const std::string &caller_usr) {
    caller_usr_hash_ = caller_usr_hash;
    caller_usr_ = caller_usr;
  }

  uint64_t get_caller_usr_hash() const { return caller_usr_hash_; }
  const auto &get_caller_usr() const { return caller_usr_; }

  int64_t get_id() const { return id_; }

 protected:
  const int64_t id_;
  const DeclInfo info_;
  uint64_t caller_usr_hash_ = 0;
  std::string caller_usr_;
};
using FuncCallPtr = std::shared_ptr<FuncCall>;

/******************************************************************************
 * Represent a function call to "function-like" macro
 *
 ******************************************************************************/
class FuncLikeMacroCall : public FuncCall {
 public:
  FuncLikeMacroCall(int64_t id, const DeclInfo &info) : FuncCall(id, info) {}
  ~FuncLikeMacroCall() = default;

  bool is_macro() const override { return true; }
};

/******************************************************************************
 * C++ class/struct definition
 *
 ******************************************************************************/
class ClassDef {
 public:
  ClassDef(const DeclInfo &info) : info_(info) {}
  ~ClassDef() = default;

  const DeclInfo &get_info() const { return info_; }

 protected:
  const DeclInfo info_;
};
using ClassDefPtr = std::shared_ptr<ClassDef>;

/** Generate USR for @d */
std::string gen_unified_symbol_resolution(const clang::Decl *d);

/** Special hash function for USR string */
uint64_t hash_usr(llvm::StringRef s);
}  // namespace pview
