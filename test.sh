#!/usr/bin/env bash
cmake --build ./build -j &&\
./build/duskc -o a.spv test.dusk &&\
spirv-val a.spv &&\
spirv-dis a.spv
