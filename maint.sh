#!/bin/sh

# 2021-03-22 Jerker Bäck

find ./src -type f \( -name "*.h" -o -name "*.cpp" \) -exec dos2unix -k -q {} \;
find ./resource -type f -name "resource.h" -exec unix2dos -k -q {} \;
find ./src -type f \( -name "*.h" -o -name "*.cpp" \) -exec clang-format -i -style=file {} \;

