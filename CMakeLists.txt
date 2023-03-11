cmake_minimum_required (VERSION 3.16)
project (pview VERSION 0.1)

include(CheckCSourceCompiles)
include(CheckCXXSourceCompiles)

set(CMAKE_CXX_STANDARD 17)

SET(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

OPTION(WITH_ASAN "Building with address sanitizer" OFF)
OPTION(USE_LD_MOLD "Use the mold linker" ON)
SET(LLVM_INSTALL_PATH "/home/yubin.ryb/projects/dbtools/llvm/llvm-project-llvmorg-14.0.6/build")

MACRO(CHECK_MOLD_LINKER_USABLE MOLD_CHECK_RESULT)
  SET(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -fuse-ld=mold")
  CHECK_C_SOURCE_COMPILES("int main(void) { return 0; }" C_LD_MOLD_RESULT
    FAIL_REGEX "unknown argument ignored"
    FAIL_REGEX "unsupported .*option"
    FAIL_REGEX "unknown .*option"
    FAIL_REGEX "unrecognized .*option"
    FAIL_REGEX "ignoring unknown option"
    FAIL_REGEX "[Ww]arning: [Oo]ption"
    FAIL_REGEX "error: visibility"
    FAIL_REGEX "warning: visibility"
    )
  CHECK_CXX_SOURCE_COMPILES("int main(void) { return 0; }" CXX_LD_MOLD_RESULT
    FAIL_REGEX "unknown argument ignored"
    FAIL_REGEX "unsupported .*option"
    FAIL_REGEX "unknown .*option"
    FAIL_REGEX "unrecognized .*option"
    FAIL_REGEX "ignoring unknown option"
    FAIL_REGEX "[Ww]arning: [Oo]ption"
    FAIL_REGEX "error: visibility"
    FAIL_REGEX "warning: visibility"
    )
  IF (C_LD_MOLD_RESULT AND CXX_LD_MOLD_RESULT)
    SET(MOLD_CHECK_RESULT 1)
  ELSE()
    SET(MOLD_CHECK_RESULT 0)
  ENDIF()
ENDMACRO()

if(NOT DEFINED CMAKE_CXX_COMPILER_ID)
  MESSAGE(FATAL_ERROR "CMAKE_CXX_COMPILER_ID not defined yet")
ENDIF()
MESSAGE(STATUS "CMAKE_CXX_COMPILER_ID: ${CMAKE_CXX_COMPILER_ID}")
MESSAGE(STATUS "CMAKE_CXX_COMPILER_VERSION: ${CMAKE_CXX_COMPILER_VERSION}")
IF(NOT (CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
  MESSAGE(WARNING "Only clang compiler is officially supported")
  MESSAGE(WARNING "Using other compiler might fail to compile this program")
ENDIF()

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -gdwarf-4")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-strict-aliasing")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_GNU_SOURCE")

IF ((CMAKE_BUILD_TYPE STREQUAL "Release") OR
    (CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"))
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")
ENDIF()

IF(WITH_ASAN)
  MESSAGE(STATUS "Building with ASAN")
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fsanitize-recover=address")
  IF(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libsan -Wno-uninitialized ")
  ELSE()
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libasan -Wno-maybe-uninitialized ")
  ENDIF()
ENDIF()

IF (USE_LD_MOLD)
  CHECK_MOLD_LINKER_USABLE(MOLD_CHECK_RESULT)
  IF (MOLD_CHECK_RESULT)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fuse-ld=mold -Wno-unused-command-line-argument ")
    MESSAGE(STATUS "Using the mold linker")
  ELSE()
    MESSAGE(WARNING "Trying the mold linker but it is not usable")
  ENDIF()
ENDIF()

# Disable building tests of gflags/glog
SET(GFLAGS_BUILD_TESTING OFF)
SET(WITH_GTEST OFF)
SET(WITH_GMOCK OFF)
SET(BUILD_TESTING OFF)

# Build static lib by default.
# This also affect subprojects such as glog/gfags/fmt, etc.
SET(BUILD_SHARED_LIBS OFF)
# mysqlcppconn use this
SET(BUILD_STATIC ON)

# gflags
EXECUTE_PROCESS(
  COMMAND ${CMAKE_COMMAND} -E tar xf ${CMAKE_SOURCE_DIR}/extra/gflags/v2.2.2.tar.gz
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/extra/gflags/
  RESULT_VARIABLE tar_result_gflags
)
add_subdirectory(extra/gflags/gflags-2.2.2)

# glog
EXECUTE_PROCESS(
  COMMAND ${CMAKE_COMMAND} -E tar xf ${CMAKE_SOURCE_DIR}/extra/glog/v0.6.0.tar.gz
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/extra/glog/
  RESULT_VARIABLE tar_result_gflags
)
add_subdirectory(extra/glog/glog-0.6.0)

# fmt
EXECUTE_PROCESS(
  COMMAND ${CMAKE_COMMAND} -E tar xf ${CMAKE_SOURCE_DIR}/extra/fmt/9.1.0.tar.gz
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/extra/fmt
  RESULT_VARIABLE tar_result_fmt
)
add_subdirectory(extra/fmt/fmt-9.1.0)

# build mysql-server
include(cmake/mysql-server.cmake)
# build mysql-connector
# mysql-connector depends on mysql-server's boost, so always build mysql-server
# before build mysql-connector.
include(cmake/mysql-connector.cmake)

# rapidjson is header-only
EXECUTE_PROCESS(
  COMMAND ${CMAKE_COMMAND} -E tar xf ${CMAKE_SOURCE_DIR}/extra/rapidjson/v1.1.0.tar.gz
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/extra/rapidjson
  RESULT_VARIABLE tar_result_rapidjson
)

# Dependencies for pview except LLVM.
# Prefer static linking.
FUNCTION(ADD_COMMON_LIB_OPTIONS target)
  TARGET_INCLUDE_DIRECTORIES(${target} SYSTEM PRIVATE ${PROJECT_ROOT}/src)

  # boost
  TARGET_INCLUDE_DIRECTORIES(${target} SYSTEM PRIVATE ${BOOST_INCLUDE_DIR})

  # std::filesystem
  TARGET_LINK_LIBRARIES(${target} PRIVATE stdc++fs)

  # gflag
  target_link_libraries(${target} PRIVATE gflags::gflags)

  # glog
  target_link_libraries(${target} PRIVATE glog::glog)

  # fmt
  target_include_directories(${target} SYSTEM PRIVATE ${PROJECT_ROOT}/extra/fmt/fmt-9.1.0/include)
  target_link_libraries(${target} PRIVATE fmt::fmt)

  # rapidjson
  target_include_directories(${target} SYSTEM PRIVATE ${PROJECT_ROOT}/extra/rapidjson/rapidjson-1.1.0/include)

  # Dynamically link OpenSSL.
  # Currently we require that OpenSSL is installed.
  # This should be OK for most system.
  # It is a prerequisite for statically linking mysql-connector
  TARGET_LINK_LIBRARIES(${target} PRIVATE ssl)
  TARGET_LINK_LIBRARIES(${target} PRIVATE crypto)

  # statically link mysql-connector
  TARGET_INCLUDE_DIRECTORIES(
    ${target}
    SYSTEM PRIVATE
    ${PROJECT_ROOT}/extra/mysql-connector/mysql-connector-cpp-8.0.13/build/jdbc/install/include/jdbc)
  TARGET_LINK_LIBRARIES(
    ${target}
    PRIVATE
    ${PROJECT_ROOT}/extra/mysql-connector/mysql-connector-cpp-8.0.13/build/libmysqlcppconn8-static.a)
  TARGET_LINK_LIBRARIES(
    ${target}
    PRIVATE
    ${PROJECT_ROOT}/extra/mysql-connector/mysql-connector-cpp-8.0.13/build/jdbc/driver/libmysqlcppconn-static.a)
  TARGET_LINK_LIBRARIES(
    ${target}
    PRIVATE
    ${PROJECT_ROOT}/extra/mysql-connector/mysql-connector-cpp-8.0.13/build/jdbc/install/lib/libmysqlcppconn-static.a)
ENDFUNCTION()

# LLVM / CLANG linking.
#
# Note:
#   - It is required that LLVM is compiled with RTTI enabled.
#     The mysql-connector we use require the use of C++ exception, and therefore
#     the whole project should enable RTTI.
FUNCTION(ADD_LLVM_LIB_OPTIONS target)
  SET(PATH_OF_CLANG_CONFIG "${LLVM_INSTALL_PATH}/lib/cmake/clang")
  SET(PATH_OF_LLVM_CONFIG "${LLVM_INSTALL_PATH}/lib/cmake/llvm")
  IF (NOT EXISTS "${PATH_OF_CLANG_CONFIG}/ClangConfig.cmake")
    MESSAGE(FATAL_ERROR
            "Cannot find ClangConfig.cmake under ${PATH_OF_CLANG_CONFIG}. "
            "Should provide the correct LLVM_INSTALL_PATH.")
  ENDIF()
  IF (NOT EXISTS "${PATH_OF_LLVM_CONFIG}/LLVMConfig.cmake")
    MESSAGE(FATAL_ERROR
            "Cannot find LLVMConfig.cmake under ${PATH_OF_LLVM_CONFIG}. "
            "Should provide the correct LLVM_INSTALL_PATH.")
  ENDIF()

  find_package(Clang REQUIRED PATHS "${PATH_OF_CLANG_CONFIG}" NO_DEFAULT_PATH)
  find_package(LLVM REQUIRED PATHS "${PATH_OF_LLVM_CONFIG}" NO_DEFAULT_PATH)

  if(NOT LLVM_ENABLE_RTTI)
    MESSAGE(FATAL_ERROR "Need a LLVM build with RTTI enabled")
  ENDIF()
  TARGET_COMPILE_OPTIONS(${target} PRIVATE -frtti)

  # Statically link libclang & llvm
  TARGET_LINK_LIBRARIES(${target} PRIVATE
    clangIndex
    clangFormat
    clangTooling
    clangToolingInclusions
    clangToolingCore
    clangFrontend
    clangParse
    clangSerialization
    clangSema
    clangAST
    clangLex
    clangDriver
    clangBasic
  )
  TARGET_LINK_LIBRARIES(${target} PRIVATE LLVMOption LLVMSupport)

  # Add include path.
  # Taken from http://maskray.me/blog/2022-10-09-c-minifier-with-clang
  FOREACH(include_dir ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
    get_filename_component(include_dir_realpath ${include_dir} REALPATH)
    # Don't add as SYSTEM if they are in CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES.
    # It would reorder the system search paths and cause issues with libstdc++'s
    # use of #include_next. See https://github.com/MaskRay/ccls/pull/417
    IF(NOT "${include_dir_realpath}" IN_LIST CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
      TARGET_INCLUDE_DIRECTORIES(${target} SYSTEM PRIVATE ${include_dir})
    ENDIF()
  ENDFOREACH()

  # system, asan, etc
  #
  # We can run `llvm-config --system-libs` to obtain llvm depency
  TARGET_LINK_LIBRARIES(${target} PRIVATE pthread)
  TARGET_LINK_LIBRARIES(${target} PRIVATE rt)
  TARGET_LINK_LIBRARIES(${target} PRIVATE dl)
  TARGET_LINK_LIBRARIES(${target} PRIVATE m)
  TARGET_LINK_LIBRARIES(${target} PRIVATE z)
  #TARGET_LINK_LIBRARIES(${target} PRIVATE tinfo)
ENDFUNCTION()

SET(
  PVIEW_SOURCES
  ${PROJECT_ROOT}/src/ClangCompiler.cc
  ${PROJECT_ROOT}/src/ExceptionAnalyzer.cc
  ${PROJECT_ROOT}/src/FileUtils.cc
  ${PROJECT_ROOT}/src/IncludeAnalyzer.cc
  ${PROJECT_ROOT}/src/IndexCtx.cc
  ${PROJECT_ROOT}/src/IndexQueue.cc
  ${PROJECT_ROOT}/src/MYSQLConn.cc
  ${PROJECT_ROOT}/src/MYSQLMgr.cc
  ${PROJECT_ROOT}/src/Main.cc
  ${PROJECT_ROOT}/src/PView.cc
  ${PROJECT_ROOT}/src/ParseTask.cc
  ${PROJECT_ROOT}/src/SQL.cc
  ${PROJECT_ROOT}/src/StrUtils.cc
  ${PROJECT_ROOT}/src/ThreadPool.cc
  ${PROJECT_ROOT}/src/siphash.cc
)
SET(PVIEW_EXECUTABLE_NAME pview)
ADD_EXECUTABLE(${PVIEW_EXECUTABLE_NAME} ${PVIEW_SOURCES})
ADD_COMMON_LIB_OPTIONS(${PVIEW_EXECUTABLE_NAME})
ADD_LLVM_LIB_OPTIONS(${PVIEW_EXECUTABLE_NAME})
ADD_DEPENDENCIES(${PVIEW_EXECUTABLE_NAME} mysqld_binary)  # build mysqld binary before building pview
ADD_DEPENDENCIES(${PVIEW_EXECUTABLE_NAME} mysql_conn_lib) # build mysql-connector before building pview
INSTALL(TARGETS ${PVIEW_EXECUTABLE_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX})
