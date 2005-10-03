#!/bin/sh
set -e

FIRST_VERSION=$(dpkg -s vdr-dev | awk '/Version/ { print $2 }')
LAST_VERSION=$(echo $FIRST_VERSION | sed -e 's/-[^-]*$//')-9999

# Set conflicts with previous and next vdr version in control
for p in $(dh_listpackages); do
    echo "vdr:Depends=vdr (>= $FIRST_VERSION)" >> debian/$p.substvars
    echo "vdr:Conflicts=vdr (>> $LAST_VERSION)" >> debian/$p.substvars
done
