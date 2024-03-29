INCLUDE(ExternalProject)

MESSAGE(STATUS "============================================")
MESSAGE(STATUS "=               Building LLVM              =")
MESSAGE(STATUS "============================================")

SET(PVIEW_LLVM_VER 14.0.6)
SET(PVIEW_LLVM_DIR         "${CMAKE_SOURCE_DIR}/extra/llvm/${PVIEW_LLVM_VER}/")
SET(PVIEW_LLVM_TAR_BALL    "${CMAKE_SOURCE_DIR}/extra/llvm/${PVIEW_LLVM_VER}/llvmorg-${PVIEW_LLVM_VER}.tar.gz")
SET(PVIEW_LLVM_SOURCE_DIR  "${CMAKE_SOURCE_DIR}/extra/llvm/${PVIEW_LLVM_VER}/llvm-project-llvmorg-${PVIEW_LLVM_VER}/")
SET(PVIEW_LLVM_BUILD_DIR   "${CMAKE_SOURCE_DIR}/extra/llvm/${PVIEW_LLVM_VER}/llvm-project-llvmorg-${PVIEW_LLVM_VER}/build" CACHE INTERNAL "PVIEW_LLVM_BUILD_DIR")
SET(PVIEW_LLVM_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/llvm")

IF(CMAKE_GENERATOR MATCHES "Makefiles")
  SET(MAKE_COMMAND ${CMAKE_MAKE_PROGRAM} -j)
ELSE() # Xcode/Ninja generators
  SET(MAKE_COMMAND make)
ENDIF()

IF(NOT (CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
  MESSAGE(WARNING "Only clang compiler is officially supported")
  MESSAGE(WARNING "Using other compiler might fail to compile this program")
  MESSAGE(FATAL_ERROR "Please use clang to compile.")
ENDIF()

SET(PVIEW_LLVM_ENABLE_PROJECTS  "clang;clang-tools-extra;lldb")
SET(PVIEW_LLVM_ENABLE_RUNTIMES  "compiler-rt;libc;libcxx;libcxxabi;libunwind")
SET(PVIEW_LLVM_CMAKE
  cmake ${PVIEW_LLVM_SOURCE_DIR}/llvm
  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
  -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
  -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
  -DCMAKE_INSTALL_PREFIX=${PVIEW_LLVM_INSTALL_DIR}
  -DLLVM_ENABLE_PROJECTS=${PVIEW_LLVM_ENABLE_PROJECTS}
  -DLLVM_ENABLE_RUNTIMES=${PVIEW_LLVM_ENABLE_RUNTIMES}
  -DLLVM_TARGETS_TO_BUILD=X86
  -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON
  -DCOMPILER_RT_DEFAULT_TARGET_ARCH=x86_64
  -DLLDB_ENABLE_LZMA=OFF
  -DLLVM_STATIC_LINK_CXX_STDLIB=ON
  -DLLVM_BUILD_LLVM_DYLIB=OFF
  -DLLVM_ENABLE_RTTI=ON
  -DLLVM_INCLUDE_BENCHMARKS=OFF
  -DLLVM_BUILD_BENCHMARKS=OFF
)

# Decompress source files
EXECUTE_PROCESS(
  COMMAND tar xf ${PVIEW_LLVM_TAR_BALL}
  WORKING_DIRECTORY ${PVIEW_LLVM_DIR}
  RESULT_VARIABLE llvm_tar_result
)
# create build dir
EXECUTE_PROCESS(
  COMMAND mkdir -p ${PVIEW_LLVM_BUILD_DIR}
  WORKING_DIRECTORY ${PVIEW_LLVM_DIR}
  RESULT_VARIABLE llvm_mkdir_result
)

ExternalProject_Add(pview_llvm
  PREFIX ${PVIEW_LLVM_INSTALL_DIR}
  SOURCE_DIR ${PVIEW_LLVM_SOURCE_DIR}
  BINARY_DIR ${PVIEW_LLVM_BUILD_DIR}
  STAMP_DIR  ${PVIEW_LLVM_BUILD_DIR}
  CONFIGURE_COMMAND ${PVIEW_LLVM_CMAKE}
  BUILD_COMMAND  ${MAKE_COMMAND}
  INSTALL_COMMAND make install -j
)

ADD_LIBRARY(llvm_binary OBJECT IMPORTED)
SET_TARGET_PROPERTIES(
  llvm_binary
  PROPERTIES IMPORTED_LOCATION "${PVIEW_LLVM_BUILD_DIR}/lib/libLLVMSupport.a")
ADD_DEPENDENCIES(llvm_binary pview_llvm)
