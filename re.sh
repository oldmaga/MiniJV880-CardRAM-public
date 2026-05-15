#!/bin/bash

    if [ -z "${RPI}" ] ; then
    echo "\$RPI missing, exiting"
    exit 1
    fi

    if [ "${RPI}" -lt "3" ]; then
    echo "Raspberry Pi ${RPI} is not supported (need >=3)"
    exit 1
    fi

    if [ -f VERSION ]; then
        VERSION=$(tr -d '\r\n' < VERSION)

        if ! git diff --quiet --ignore-submodules -- 2>/dev/null || \
           ! git diff --cached --quiet --ignore-submodules -- 2>/dev/null; then
            VERSION="${VERSION}-dirty"
        fi
    elif git describe --tags --dirty --always --long > /dev/null 2>&1; then
        VERSION=$(git describe --tags --dirty --always --long)
    else
        VERSION="unknown"
    fi


# Create version.h
cat > src/version.h << EOF
#ifndef VERSION_H
#define VERSION_H

#define VERSION_STRING "${VERSION}"
#define VERSION_SHORT "${VERSION%%-*}"  
#define VERSION_IS_DIRTY "${VERSION##*-*-*}" == "dirty" || echo "clean"

#endif // VERSION_H
EOF
  
# Build MiniJV880
cd src
make clean || true
make -j
ls *.img
cd ..

mkdir -p releases
# cp src/kernel8.img "releases/MiniJV880-${VERSION}.img"
