#!/bin/sh
set -e

FIRST_VERSION=$(dpkg -s vdr-dev | awk '/Version/ { print $2 }')

# Set conflicts with previous vdr version in control
for p in $(dh_listpackages); do
    echo "vdr:Depends=vdr (>= $FIRST_VERSION)" >> debian/$p.substvars
done
