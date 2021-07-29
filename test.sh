#!/usr/bin/env bash

set -o errexit

if [ ! -d out ]; then
    meson out
fi
meson install -C out

export LD_LIBRARY_PATH=/home/patrick/external/art/vendor/Vulkan/packaged/lib/
pushd data
gdb -ex 'break context.hpp:590' -ex run --args ../packaged/bin/triangleAnimated
#valgrind ../packaged/bin/triangleAnimated
#../packaged/bin/triangleAnimated
