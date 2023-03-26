#!/bin/bash -x

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
echo "Directory of current script: $SCRIPT_DIR"
cd ${SCRIPT_DIR}

root_dir=`pwd`
default_build_dir=`pwd`/build
rm -rf $default_build_dir && mkdir -p $default_build_dir && cd $default_build_dir

build_type="release"
#inst_dir="/usr/local/dbtools/pview"
inst_dir="${root_dir}/install"
san_type=""
asan=0
tsan=0

get_key_value()
{
  echo "$1" | sed 's/^-[a-zA-Z_-]*=//'
}

usage()
{
cat <<EOF
Usage: $0 [-t debug|release] [-d <inst_dir>]
       Or
       $0 [-h | --help]
  -t   Select the build type: release, debug.
  -d   Set the destination directory.
  -g   Enable the sanitizer of compiler,
       asan for AddressSanitizer, tsan for ThreadSanitizer
  -h, --help              Show this help message.
EOF
}

parse_options()
{
  while test $# -gt 0
  do
    case "$1" in
    -t=*)
      build_type=`get_key_value "$1"`;;
    -t)
      shift
      build_type=`get_key_value "$1"`;;
    -d=*)
      inst_dir=`get_key_value "$1"`;;
    -d)
      shift
      inst_dir=`get_key_value "$1"`;;
    -g=*)
      san_type=`get_key_value "$1"`;;
    -g)
      shift
      san_type=`get_key_value "$1"`;;
    -h | --help)
      usage
      exit 0;;
    *)
      echo "Unknown option '$1'"
      exit 1;;
    esac
    shift
  done
}

dump_options()
{
  echo "Dumping the options used by $0 ..."
  echo "build_type=$build_type"
  echo "Sanitizer=$san_type"
}

download_files()
{
  # Download LLVM source code from github if missed
  llvm_tar_ball_path=${SCRIPT_DIR}/extra/llvm/14.0.6/llvmorg-14.0.6.tar.gz
  llvm_tar_ball_md5_checksum=52e6c9ea5267274bffd5f0f5ba24e076
  llvm_download_url=https://github.com/llvm/llvm-project/archive/refs/tags/llvmorg-14.0.6.tar.gz
  llvm_download_timeout=300
  if [ ! -f "${llvm_tar_ball_path}" ]; then
    echo "Downloading LLVM source code from ${llvm_download_url} ..."
    echo "LLVM tar ball will be put at: ${llvm_tar_ball_path}"
    wget ${llvm_download_url} -O ${llvm_tar_ball_path} --timeout=${llvm_download_timeout}
    if [[ $? -eq 0 ]]; then
      echo "LLVM tar ball downloaded."
    else
      echo "Failed to download LLVM tar ball. Please download manually from ${llvm_download_url} and put it at ${llvm_tar_ball_path}"
      rm -f ${llvm_tar_ball_path}
      exit 1
    fi
  else
    file_md5_checksum=$(md5sum ${llvm_tar_ball_path} |awk '{print $1}')
    if [ "${file_md5_checksum}" = "${llvm_tar_ball_md5_checksum}" ]; then
      echo "LLVM tar ball exists."
    else
      echo "LLVM tar ball checksump mismatch. Expected ${llvm_tar_ball_md5_checksum}, but got ${file_md5_checksum}"
      exit 1
    fi
  fi
}

parse_options "$@"
dump_options
download_files

if [ x"$build_type" = x"debug" ]; then
  build_type="Debug"
elif [ x"$build_type" = x"release" ]; then
  build_type="RelWithDebInfo"
else
  echo "Invalid build type, it must be \"debug\", \"release\" ."
  exit 1
fi

if [ x"$san_type" = x"" ]; then
    asan=0
    tsan=0
elif [ x"$san_type" = x"asan" ]; then
    asan=1
    tsan=0
elif [ x"$san_type" = x"tsan" ]; then
    asan=0
    tsan=1
else
  echo "Invalid sanitizer type, it must be \"asan\" or \"tsan\"."
  exit 1
fi

cmake \
  -DCMAKE_C_COMPILER=/usr/local/dbtools/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/usr/local/dbtools/llvm/bin/clang++ \
  -DCMAKE_BUILD_TYPE="$build_type"   \
  -DCMAKE_INSTALL_PREFIX="$inst_dir"   \
  -DCMAKE_EXE_LINKER_FLAGS=" -static-libstdc++ -static-libgcc " \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=1 \
  -DWITH_ASAN=$asan                 \
  ../

cp ./compile_commands.json ../

make install -j
