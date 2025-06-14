#!/bin/sh

BASE_PATH="./jsfx"

find "$BASE_PATH" -type f \( -iname "*.h" -o -iname "*.hpp" \) | while read -r file; do
    dir=$(dirname "$file")
    base=$(basename "$file")
    lowerbase=$(echo "$base" | tr '[:upper:]' '[:lower:]')
    if [ "$base" != "$lowerbase" ]; then
        mv "$file" "$dir/$lowerbase"
    fi
done

find "$BASE_PATH" -type f \( -iname "*.cpp" -o -iname "*.c" -o -iname "*.h" -o -iname "*.hpp" \) | while read -r src; do
    tmpfile="${src}.tmp"
    sed -E 's/(#include[[:space:]]*")([^"]+)(")/echo "\1$(echo \2 | tr [:upper:] [:lower:])\3"/ge' "$src" > "$tmpfile" && mv "$tmpfile" "$src"
done