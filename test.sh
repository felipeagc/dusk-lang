#!/usr/bin/env bash
cmake --build ./build -j &&\
./build/duskc -o a.spv test.dusk &&\
spirv-dis --raw-id a.spv &&\
spirv-val a.spv
