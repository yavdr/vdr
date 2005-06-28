#!/bin/sh
set -e
case "$*" in
    "make")
        # scan patches in 00list and write to patchlevel file
        echo -n "Patches: "
        for p in $(grep "^opt-[0-9][0-9]_" debian/patches/00list | cut -d"_" -f2-); do
            echo -n "$p "
            PATCHES="$PATCHES $p"
        done
        echo
        echo "patchlevel=$PATCHES" > patchlevel
        ;;
    "clean")
        # remove patchlevel file
        rm -f patchlevel
        ;;
    "subst")
        # read patchlevel file and write contents to *.substvars
        if [ -r patchlevel ]; then
            # main vdr package
            PATCHES=$(cat patchlevel)
        else
            # vdr-plugin package
            PATCHES=$(cat /usr/include/vdr/patchlevel)
        fi
        # write *.substvars only if patchlevel not empty
        if [ "$PATCHES" != "patchlevel=" ]; then
            # scan control for packages
            for p in $(dh_listpackages); do
                echo "$PATCHES" >> debian/$p.substvars
            done
        fi
        ;;
    *)
        echo >&2 "$0: script expects make|clean|subst as argument"
        exit 1
        ;;
esac
exit 0
