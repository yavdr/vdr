# dependencies.sh - vdr plugins depend on the current vdr version
#
# This script is called in debian/rules of vdr plugins:
#    sh /usr/share/vdr-dev/dependencies.sh
#
# It sets a dependency to the current vdr version for all binary packages of the
# plugin package. The current vdr version is the version of the vdr-dev package
# used to compile the plugin package.
#
# This script sets the substitution variable "vdr:Depends" which is used in the
# debian/control file of vdr plugins, e.g.:
#    Depends: ${vdr:Depends}

set -e

FIRST_VERSION=$(dpkg -s vdr-dev | awk '/Version/ { print $2 }')

# Set conflicts with previous vdr version in control
for p in $(dh_listpackages); do
    echo "vdr:Depends=vdr (>= $FIRST_VERSION)" >> debian/$p.substvars
done
