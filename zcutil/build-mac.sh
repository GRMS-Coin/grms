#!/bin/bash
export CC=gcc-8
export CXX=g++-8
export LIBTOOL=libtool
export AR=ar
export RANLIB=ranlib
export STRIP=strip
export OTOOL=otool
export NM=nm

set -eu -o pipefail

# Allow users to set arbitrary compile flags. Most users will not need this.
if [[ -z "${CONFIGURE_FLAGS-}" ]]; then
    CONFIGURE_FLAGS=""
fi

if [ "x$*" = 'x--help' ]
then
    cat <<EOF
Usage:

$0 --help
  Show this help message and exit.

$0 [ --enable-lcov ] [ --enable-websockets ] [ --enable-debug ] [ MAKEARGS... ]
  Build Komodo and most of its transitive dependencies from
  source. MAKEARGS are applied to both dependencies and Komodo itself. 
  If --enable-lcov is passed, Komodo is configured to add coverage
  instrumentation, thus enabling "make cov" to work.
  If --enable-websockets is passed then websockets support is added for nSPV protocol (disabled for now)
  If --enable-debug is passed, Komodo is built with debugging information. It
  must be passed after the previous arguments, if present.
EOF
    exit 0
fi

# If --enable-lcov is the first argument, enable lcov coverage support:
LCOV_ARG=''
HARDENING_ARG='--disable-hardening'
if [ "x${1:-}" = 'x--enable-lcov' ]
then
    LCOV_ARG='--enable-lcov'
    HARDENING_ARG='--disable-hardening'
    shift
fi

# If --enable-websockets is the next argument, enable websockets support for nspv clients:
WEBSOCKETS_ARG=''
# if [ "x${1:-}" = 'x--enable-websockets' ]
# then
#    WEBSOCKETS_ARG='--enable-websockets=yes'
#    shift
#fi

# If --enable-debug is the next argument, enable debugging
DEBUGGING_ARG=''
if [ "x${1:-}" = 'x--enable-debug' ]
then
    DEBUG=1
    export DEBUG
    DEBUGGING_ARG='--enable-debug'
    shift
fi

TRIPLET=`./depends/config.guess`
PREFIX="$(pwd)/depends/$TRIPLET"

make "$@" -C ./depends/ V=1 NO_QT=1 NO_PROTON=1

./autogen.sh
CPPFLAGS="-I$PREFIX/include -arch x86_64" LDFLAGS="-L$PREFIX/lib -arch x86_64 -Wl,-no_pie" \
CXXFLAGS="-arch x86_64 -I/usr/local/Cellar/gcc\@8/8.3.0/include/c++/8.3.0/ -fwrapv -fno-strict-aliasing -Wno-builtin-declaration-mismatch -Werror -Wno-error=attributes -g -Wl,-undefined -Wl,dynamic_lookup" \
./configure --prefix="${PREFIX}" --with-gui=no "$HARDENING_ARG" "$LCOV_ARG" "$CONFIGURE_FLAGS" "$WEBSOCKETS_ARG"  "$DEBUGGING_ARG" \
  --with-custom-bin=yes CUSTOM_BIN_NAME=grms CUSTOM_BRAND_NAME=GRMS \
  CUSTOM_SERVER_ARGS="'-ac_name=GRMS -ac_supply=100000 -ac_reward=1200000000,900000000,10000000 -ac_end=180700,900000,3600000 -ac_algo=verushash -ac_adaptivepow=6 -ac_eras=3 -ac_blocktime=30 -ac_veruspos=50 -ac_cbmaturity=3 -ac_cc=333 -ac_sapling=1 -addnode=91.231.187.163 -addnode=91.231.187.130 -addnode=91.231.187.105 -addnode=91.231.187.20 -addnode=91.231.187.113 -addnode=91.231.187.115 -addnode=91.231.187.116 -addnode=91.231.187.120 -nspv_msg=1'" \
  CUSTOM_CLIENT_ARGS='-ac_name=GRMS'

make "$@" V=1 NO_GTEST=1 STATIC=1 
