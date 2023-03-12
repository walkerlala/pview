The development of the pview project refer to example code of
the following projects:

  - [ccls](https://github.com/MaskRay/ccls)
  The ccls language server project inspire the core structure of the pview
  indexer: how to using clang frontend to index code, how to uniquely indentify
  C/C++ symbols, etc.

  - [LLVM clang-tidy](https://github.com/llvm/llvm-project/tree/main/clang-tools-extra/clang-tidy)
  The ExceptionAnalyzer of the pview indexer is borrowed from the LLVM clang-tidy tool.
