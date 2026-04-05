@echo off
set CC=clang --target=x86_64-w64-windows-gnu -fuse-ld=lld
set CXX=clang++ --target=x86_64-w64-windows-gnu -fuse-ld=lld
meson setup build
@echo on