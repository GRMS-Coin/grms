#!/bin/bash
export HOST=x86_64-w64-mingw32
CXX=x86_64-w64-mingw32-g++-posix
CC=x86_64-w64-mingw32-gcc-posix
PREFIX="$(pwd)/depends/$HOST"

set -eu -o pipefail

UTIL_DIR="$(dirname "$(readlink -f "$0")")"
BASE_DIR="$(dirname "$(readlink -f "$UTIL_DIR")")"
PREFIX="$BASE_DIR/depends/$HOST"

# disable for code audit
# If --enable-websockets is the next argument, enable websockets support for nspv clients:
WEBSOCKETS_ARG=''
# if [ "x${1:-}" = 'x--enable-websockets' ]
# then
# WEBSOCKETS_ARG='--enable-websockets=yes'
# shift
# fi

# make dependences
cd depends/ && make HOST=$HOST V=1 NO_QT=1
cd ../

cd $BASE_DIR/depends
make HOST=$HOST NO_QT=1 "$@"
cd $BASE_DIR

./autogen.sh
CONFIG_SITE=$BASE_DIR/depends/$HOST/share/config.site CXXFLAGS="-DPTW32_STATIC_LIB -DCURL_STATICLIB -DCURVE_ALT_BN128 -fopenmp -pthread" ./configure --prefix=$PREFIX --host=$HOST --enable-static --disable-shared "$WEBSOCKETS_ARG" \
  --with-custom-bin=yes CUSTOM_BIN_NAME=grms CUSTOM_BRAND_NAME=GRMS \
  CUSTOM_SERVER_ARGS="'-ac_name=GRMS -ac_supply=100000 -ac_reward=1200000000,900000000,10000000 -ac_end=180700,900000,3600000 -ac_algo=verushash -ac_adaptivepow=6 -ac_eras=3 -ac_blocktime=30 -ac_veruspos=50 -ac_cbmaturity=3 -ac_cc=333 -ac_sapling=1 -addnode=91.231.187.163 -addnode=91.231.187.130 -addnode=91.231.187.105 -addnode=91.231.187.20 -addnode=91.231.187.113 -addnode=91.231.187.115 -addnode=91.231.187.116 -addnode=91.231.187.120 -nspv_msg=1'" \
  CUSTOM_CLIENT_ARGS='-ac_name=GRMS'
sed -i 's/-lboost_system-mt /-lboost_system-mt-s /' configure 
  
cd src/
# note: to build grmsd, grms-cli it should not exist 'komodod.exe komodo-cli.exe' param here:
CC="${CC} -g " CXX="${CXX} -g " make V=1    
