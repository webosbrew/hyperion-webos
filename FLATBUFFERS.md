# Flatbuffers

Flatcc: https://github.com/dvidelabs/flatcc

This document describes how to update included flatbuffer files
(library & auto generated packet definitions)

## Preparation

```sh
# TODO: Adjust to your setup
export TOOLCHAIN_FILE=<TOOLCHAIN_DIR>/arm-webos-linux-gnueabi_sdk-buildroot/share/buildroot/toolchainfile.cmake
export HYPERION_WEBOS_DIR=<CHANGE ME>

git clone https://github.com/dvidelabs/flatcc
cd flatcc/
```

## Building the library

```
mkdir -p build/xbuild
pushd ./build/xbuild

cmake ../.. \
 -DBUILD_SHARED_LIBS=on \
 -DCMAKE_BUILD_TYPE=Release \
 -DFLATCC_TEST=off \
 -DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}
make

popd

# Copy built library
cp ./lib/libflatccrt.a ${HYPERION_WEBOS_DIR}/flatccrt/lib/

# Remove old and copy new headers
rm -rf ${HYPERION_WEBOS_DIR}/flatccrt/include/flatcc
cp -r ./include/flatcc ${HYPERION_WEBOS_DIR}/flatccrt/include/
```

## Generating the header files

```sh
mkdir -p build
pushd build

cmake .. -DCMAKE_BUILD_TYPE=Release
make

popd

# Generate headers
./bin/flatcc ${HYPERION_WEBOS_DIR}/fbs/hyperion_request.fbs -w --common_reader --common_builder
./bin/flatcc ${HYPERION_WEBOS_DIR}/fbs/hyperion_reply.fbs -w --common_reader --common_builder

# Overwrite existing headers
cp *.h ${HYPERION_WEBOS_DIR}/fbs/
```