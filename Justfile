#! /usr/bin/env just

default:
    @just --list

make: clean
    #! /usr/bin/env bash
    mkdir -p bin/debug/native
    cd bin/debug/native
    # ../../../configure --enable-debug
    ../../../configure --target-list=aarch64-softmmu
    make


clean:
    #! /usr/bin/env bash
    if [ -d "bin/debug/native" ]; then
        rm -rf bin/debug/native
    fi
