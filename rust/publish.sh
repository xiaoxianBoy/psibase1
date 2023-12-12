#!/bin/bash

###### 
#   ~ Dependencies ~
#
#                               ,---> fracpack -->--,
#   cargo-psibase --> psibase --|                   |
#                               |--->---------------'--> psibase-macros -->--,
#                               |                                             |
#                               '--->-----------------------------------------'--> psibase-names
######

# Dependencies above imply the following publish order:
dirs=(psibase_names psibase_macros fracpack psibase cargo-psibase)

cd "$(dirname "$0")"

# Ensure each crate builds
for dir in "${dirs[@]}"; do
    cd "$dir" || exit
    echo "Building $dir..."
    cargo build || exit
    cd ..
done

# Now publish each crate
for dir in "${dirs[@]}"; do
    cd "$dir"
    echo "Publishing $dir..."
    cargo publish --dry-run
    cd ..
done
