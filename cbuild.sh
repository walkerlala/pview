#!/bin/bash -x

cd "$(dirname "$0")"

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

parse_options "$@"
dump_options

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
