#
# make-special-vdr.sh by Thomas Günther <tom@toms-cafe.de>
#
# Description:
#
# Make a special variation of the vdr package or of a vdr plugin package.
# These debian packages could be installed parallel to the standard vdr debian
# packages in order to test new development versions of vdr. Between the
# standard and the special variation of vdr can be switched via command menu.
#
# Standard and special packages uses the same recordings directory. Therefore,
# user and group are 'vdr' for the special packages too. The suffix of recording
# files remains also '.vdr'.
#
# Necessary adaptions in the debian source packages:
#
# The special packages are built from the same source packages as the standard
# packages. The make-special-vdr.sh script is called from "debian/rules" instead
# of the normal make. In the first line of "debian/rules" "#! /usr/bin/make -f"
# has to be replaced with "#! /bin/sh debian/make-special-vdr.sh" for the vdr
# package respectively with "#! /bin/sh /usr/share/vdr-dev/make-special-vdr.sh"
# for a vdr plugin package.
#
# Usage:
#
# The name of the special package is specified by the environment variable
# SPECIAL_VDR_SUFFIX. E.g., the vdrdevel variation is built with
#    SPECIAL_VDR_SUFFIX=devel fakeroot dpkg-buildpackage -us -uc -tc
#
# The plugin packages don't include make-special-vdr.sh themselves. Instead they
# use /usr/share/vdr-dev/make-special-vdr.sh installed by the vdr-dev package.
#
# If the installed make-special-vdr.sh version of vdr-dev is to old to build the
# special variation of a particular vdr plugin package, a newer version of
# make-special-vdr.sh can be specified by the environment variable
# MAKE_SPECIAL_VDR, e.g.
#    export MAKE_SPECIAL_VDR=/usr/share/vdrdevel-dev/make-special-vdr.sh
#    SPECIAL_VDR_SUFFIX=devel fakeroot dpkg-buildpackage -us -uc -tc
#
# In order to build the standard vdr packages the environment variable
# SPECIAL_VDR_SUFFIX has to be empty or not set.
#
# Implementation details:
#
# If SPECIAL_VDR_SUFFIX is set and not empty make-special-vdr.sh does following
# steps:
#    1. Create the subdirectory ".save".
#    2. Copy all files and directories into ".save".
#    3. Substitute "vdr" in the contents of all files (recursively) except for
#       "debian/changelog", "debian/make-special-vdr.sh", all files in
#       "debian/plugin-template", and all files in ".save".
#    4. Substitute "vdr" in the names of all files (recursively) except for all
#       files in ".save".
#    5. Make special changes for certain packages.
#    6. Call the normal make.
# Points 1-5 are performed only if the subdirectory ".save" not exist.
# If "debian/rules" is called with the argument "clean" all original files and
# directories are restored form ".save" and the subdirectory ".save" is removed.
#
# History:
#
#    2004-06-12 - 2005-09-29: Version 0.0.0 - 0.1.4 (vdrdevel patch)
#
#    2007-01-13: Version 0.2
#       - Converted vdrdevel patch to make-special-vdr.sh
#
#    2007-01-24: Version 0.3
#       - Fixed detection of *.vdr files in burn plugin
#
#    2007-02-11: Version 0.4
#       - Updated prepare_vompserver for new vompserver release 0.2.6
#       - Fixed prepare_vdrc
#       - Exit immediately on errors
#
#    2007-02-27: Version 0.5
#       - Updated prepare_graphtft for new graphtft release 0.0.16
#
#    2007-06-27: Version 0.6
#       - Fixed prepare_xineliboutput
#       - Added prepare_burn to use backgrounds from standard packages
#       - Fixed substitions for debianize-vdrplugin in prepare_vdr
#       - Fixed detection of *.vdr files in vompserver plugin
#
#    2008-02-10: Version 0.7
#       - Updated prepare_text2skin to use skin locales from standard packages
#       - Added substition for vdr-skins suggestion in prepare_text2skin
#       - Exclude documentation files (README etc.) from substitions
#       - Preserve mode, ownership and timestamps
#
#    2008-03-24: Version 0.8
#       - Updated prepare_sudoku and prepare_wapd for cdbs build system
#       - Added prepare_osdteletext (no substitution in README)
#       - Updated substitution for the plugin debianizer script in prepare_vdr
#
#    2008-04-16: Version 0.9
#       - Updated prepare_softdevice for cdbs build system
#       - Use version of vdr-dev instead of vdrdevel-dev in the plugin
#         debianizer script


main()
{
    set -e
    echo "$0" "$@" \
         "SPECIAL_VDR_SUFFIX='${SPECIAL_VDR_SUFFIX}'" \
         "MAKE_SPECIAL_VDR='${MAKE_SPECIAL_VDR}'" \
         "NO_CHECKBUILDDEPS='${NO_CHECKBUILDDEPS}'"
    if [ -z "${SPECIAL_VDR_SUFFIX}" ]; then
        # Original make if SPECIAL_VDR_SUFFIX is not set
        /usr/bin/make -f "$@"
    elif [ "${MAKE_SPECIAL_VDR}" ]; then
        # Call newer version of make-special-vdr.sh provided by MAKE_SPECIAL_VDR
        MAKE_SPECIAL_VDR= /bin/sh "${MAKE_SPECIAL_VDR}" "$@"
    elif ! check_clean_arg "$@"; then
        # Make special variation: prepare the package before make
        prepare
        /usr/bin/make -f "$@"
    else
        # Clean prepared package
        cleanup
    fi
}

SAVE_DIR=".save"

prepare()
{
    if [ ! -e "${SAVE_DIR}" ]; then
        echo "prepare: save all in subdirectory ${SAVE_DIR}"
        /bin/mkdir "${SAVE_DIR}"
        /bin/chmod -R +w .
        /bin/cp -af $(/usr/bin/find ./ -mindepth 1 -maxdepth 1 \
                                    -not -name "${SAVE_DIR}") "${SAVE_DIR}"

        # Create tempfile
        TMP_FILE=$(/bin/mktemp)

        # Execute substitutions
        prepare_common
        if check_package "vdr${SPECIAL_VDR_SUFFIX}"; then
            prepare_vdr
        fi
        if check_package "vdr${SPECIAL_VDR_SUFFIX}-plugin-analogtv"; then
            prepare_analogtv
        fi
        if check_package "vdr${SPECIAL_VDR_SUFFIX}-plugin-burn"; then
            prepare_burn
        fi
        if check_package "vdr${SPECIAL_VDR_SUFFIX}-plugin-graphtft"; then
            prepare_graphtft
        fi
        if check_package "vdr${SPECIAL_VDR_SUFFIX}-plugin-mediamvp"; then
            prepare_mediamvp
        fi
        if check_package "vdr${SPECIAL_VDR_SUFFIX}-plugin-osdteletext"; then
            prepare_osdteletext
        fi
        if check_package "vdr${SPECIAL_VDR_SUFFIX}-plugin-pin"; then
            prepare_pin
        fi
        if check_package "vdr${SPECIAL_VDR_SUFFIX}-plugin-rssreader"; then
            prepare_rssreader
        fi
        if check_package "vdr${SPECIAL_VDR_SUFFIX}-plugin-softdevice"; then
            prepare_softdevice
        fi
        if check_package "vdr${SPECIAL_VDR_SUFFIX}-plugin-sudoku"; then
            prepare_sudoku
        fi
        if check_package "vdr${SPECIAL_VDR_SUFFIX}-plugin-text2skin"; then
            prepare_text2skin
        fi
        if check_package "vdr${SPECIAL_VDR_SUFFIX}-plugin-vdr${SPECIAL_VDR_SUFFIX}c"; then
            prepare_vdrc
        fi
        if check_package "vdr${SPECIAL_VDR_SUFFIX}-plugin-vdr${SPECIAL_VDR_SUFFIX}cd"; then
            prepare_vdrcd
        fi
        if check_package "vdr${SPECIAL_VDR_SUFFIX}-plugin-vdr${SPECIAL_VDR_SUFFIX}rip"; then
            prepare_vdrrip
        fi
        if check_package "vdr${SPECIAL_VDR_SUFFIX}-plugin-vompserver"; then
            prepare_vompserver
        fi
        if check_package "vdr${SPECIAL_VDR_SUFFIX}-plugin-wapd"; then
            prepare_wapd
        fi
        if check_package "vdr${SPECIAL_VDR_SUFFIX}-plugin-xine"; then
            prepare_xine
        fi
        if check_package "vdr${SPECIAL_VDR_SUFFIX}-plugin-xineliboutput"; then
            prepare_xineliboutput
        fi

        # Check build dependencies after substitutions
        if [ -z "${NO_CHECKBUILDDEPS}" ]; then
            if ! /usr/bin/dpkg-checkbuilddeps; then
                echo >&2 "Build dependencies/conflicts unsatisfied; aborting."
                echo >&2 "(Set NO_CHECKBUILDDEPS environment variable to override.)"
                exit 3
            fi
        fi

        # Remove tempfile
        /bin/rm -f "${TMP_FILE}"
    fi
}

prepare_common()
{
    echo "prepare_common: substitute vdr -> vdr${SPECIAL_VDR_SUFFIX}"
    SUBST="s.vdr.vdr${SPECIAL_VDR_SUFFIX}.g; \
           s.make-special-vdr${SPECIAL_VDR_SUFFIX}.make-special-vdr.g; \
           s./bin/sh /usr/share/vdr${SPECIAL_VDR_SUFFIX}-dev/make-special-vdr./bin/sh /usr/share/vdr-dev/make-special-vdr.g; \
           s.Source: vdr${SPECIAL_VDR_SUFFIX}.Source: vdr.g; \
           s.Source: vdr-plugin-vdr${SPECIAL_VDR_SUFFIX}c.Source: vdr-plugin-vdrc.g; \
           s.Source: vdr-plugin-vdr${SPECIAL_VDR_SUFFIX}rip.Source: vdr-plugin-vdrrip.g; \
           s.Source: vdr-plugin-svdr${SPECIAL_VDR_SUFFIX}p.Source: vdr-plugin-svdrp.g; \
           s.pkg-vdr${SPECIAL_VDR_SUFFIX}-dvb-devel.pkg-vdr-dvb-devel.g; \
           s.vdr${SPECIAL_VDR_SUFFIX}admin.vdradmin.g; \
           s.vdr${SPECIAL_VDR_SUFFIX}-xxv.vdr-xxv.g; \
           s.vdr${SPECIAL_VDR_SUFFIX}sync.vdrsync.g; \
           s.vdr${SPECIAL_VDR_SUFFIX}-xpmlogos.vdr-xpmlogos.g; \
           s.vdr${SPECIAL_VDR_SUFFIX}-genindex.vdr-genindex.g; \
           s.USER=vdr${SPECIAL_VDR_SUFFIX}.USER=vdr.g; \
           s.GROUP=vdr${SPECIAL_VDR_SUFFIX}.GROUP=vdr.g; \
           s.chown vdr${SPECIAL_VDR_SUFFIX}:vdr${SPECIAL_VDR_SUFFIX}.chown vdr:vdr.g; \
           s.chown -R vdr${SPECIAL_VDR_SUFFIX}:vdr${SPECIAL_VDR_SUFFIX}.chown -R vdr:vdr.g; \
           s/resume%s%s\.vdr${SPECIAL_VDR_SUFFIX}/resume%s%s.vdr/g; \
           s/summary\.vdr${SPECIAL_VDR_SUFFIX}/summary.vdr/g; \
           s/info\.vdr${SPECIAL_VDR_SUFFIX}/info.vdr/g; \
           s/marks\.vdr${SPECIAL_VDR_SUFFIX}/marks.vdr/g; \
           s/index\.vdr${SPECIAL_VDR_SUFFIX}/index.vdr/g; \
           s/%03d\.vdr${SPECIAL_VDR_SUFFIX}/%03d.vdr/g; \
           s/%03i\.vdr${SPECIAL_VDR_SUFFIX}/%03i.vdr/g; \
           s/dvd\.vdr${SPECIAL_VDR_SUFFIX}/dvd.vdr/g; \
           s/001\.vdr${SPECIAL_VDR_SUFFIX}/001.vdr/g; \
           s/002\.vdr${SPECIAL_VDR_SUFFIX}/002.vdr/g; \
           s/index_%02d\.vdr${SPECIAL_VDR_SUFFIX}/index_%02d.vdr/g; \
           s/\[0-9\]\.vdr${SPECIAL_VDR_SUFFIX}/[0-9].vdr/g; \
           s/\"\.vdr${SPECIAL_VDR_SUFFIX}\"/\".vdr\"/g; \
           s/}\.vdr${SPECIAL_VDR_SUFFIX}/}.vdr/g; \
           s/index_archive\.vdr${SPECIAL_VDR_SUFFIX}/index_archive.vdr/g; \
           s/{TRACK_ON_DVD}\.vdr${SPECIAL_VDR_SUFFIX}/{TRACK_ON_DVD}.vdr/g; \
           s/size_cut\.vdr${SPECIAL_VDR_SUFFIX}/size_cut.vdr/g; \
           s/size\.vdr${SPECIAL_VDR_SUFFIX}/size.vdr/g; \
           s/strcasecmp(pos, \".vdr${SPECIAL_VDR_SUFFIX}\")/strcasecmp(pos, \".vdr\")/g; \
           s/input_vdr${SPECIAL_VDR_SUFFIX}\.h/input_vdr.h/g; \
           s/dvdr${SPECIAL_VDR_SUFFIX}ecord/dvdrecord/g; \
           s/dvdr${SPECIAL_VDR_SUFFIX}ead/dvdread/g"
    FILES=$(/usr/bin/find ./ -type f -not -regex "./${SAVE_DIR}/.*" \
                          -not -regex "./debian/changelog" \
                          -not -regex "./debian/copyright" \
                          -not -regex "./debian/make-special-vdr.sh" \
                          -not -regex "./debian/plugin-template/.*" \
                          -not -regex "./README.*" \
                          -not -regex "./LIESMICH.*" \
                          -not -regex "./AUTHORS.*" \
                          -not -regex "./CONTRIBUTORS.*" \
                          -not -regex "./FAQ.*" \
                          -not -regex "./MANUAL.*" \
                          -not -regex "./TODO.*" \
                          -not -regex "./TROUBLESHOOTING.*")
    set -f; OLD_IFS="${IFS}"; IFS="
"; set -- ${FILES}; IFS="${OLD_IFS}"; set +f
    subst_in_files "${SUBST}" "$@"
    rename_files   "${SUBST}" "$@"
}

prepare_vdr()
{
    echo "prepare_vdr: prevent conflict to standard vdr"
    if /bin/grep -q "var/lib/video" "debian/vdr${SPECIAL_VDR_SUFFIX}.links"; then
        /bin/sed -e "s/\${SPECIAL_VDR_SUFFIX}/${SPECIAL_VDR_SUFFIX}/g" <<'EOF' | /usr/bin/patch -p0 -F0
--- debian/vdr${SPECIAL_VDR_SUFFIX}.links
+++ debian/vdr${SPECIAL_VDR_SUFFIX}.links
@@ -15 +14,0 @@
-var/lib/video.00  var/lib/video
--- debian/vdr${SPECIAL_VDR_SUFFIX}.postinst
+++ debian/vdr${SPECIAL_VDR_SUFFIX}.postinst
@@ -1,1 +1,6 @@
-#!/bin/sh
+#!/bin/sh
+if [ "$1" = "configure" ]; then
+    if [ ! -e /var/lib/video ] ; then
+        /bin/ln -s /var/lib/video.00 /var/lib/video
+    fi
+fi
EOF
    fi

    echo "prepare_vdr: add debconf question which vdr variation should start automatically"
    if [ ! -e "debian/vdr${SPECIAL_VDR_SUFFIX}.config" ]; then
        /bin/sed -e "s/\${SPECIAL_VDR_SUFFIX}/${SPECIAL_VDR_SUFFIX}/g" <<'EOF' | /usr/bin/patch -p0 -F0
--- debian/rules
+++ debian/rules
@@ -130 +130 @@
-	dh_installinit -a
+	dh_installinit -a --noscripts
--- debian/vdr${SPECIAL_VDR_SUFFIX}.config  1970-01-01 00:00:00.000000000 +0000
+++ debian/vdr${SPECIAL_VDR_SUFFIX}.config
@@ -0,0 +1,6 @@
+#! /bin/sh
+set -e
+. /usr/share/debconf/confmodule
+db_version 2.0
+db_input high vdr${SPECIAL_VDR_SUFFIX}/autostart || true
+db_go || true
--- debian/vdr${SPECIAL_VDR_SUFFIX}.postinst
+++ debian/vdr${SPECIAL_VDR_SUFFIX}.postinst
@@ -1,1 +1,21 @@
-#!/bin/sh
+#!/bin/sh
+. /usr/share/debconf/confmodule
+if [ "$1" = "configure" ]; then
+    db_get vdr${SPECIAL_VDR_SUFFIX}/autostart
+    if [ "$RET" = "true" ]; then
+        START="vdr${SPECIAL_VDR_SUFFIX}"
+        STOP="vdr"
+    else
+        START="vdr"
+        STOP="vdr${SPECIAL_VDR_SUFFIX}"
+    fi
+    if [ -x "/etc/init.d/$STOP" ]; then
+        /etc/init.d/$STOP stop
+        update-rc.d -f $STOP remove >/dev/null
+    fi
+    if [ -x "/etc/init.d/$START" ]; then
+        /etc/init.d/$START stop
+        update-rc.d $START defaults >/dev/null
+        /etc/init.d/$START start
+    fi
+fi
--- debian/vdr${SPECIAL_VDR_SUFFIX}.prerm  1970-01-01 00:00:00.000000000 +0000
+++ debian/vdr${SPECIAL_VDR_SUFFIX}.prerm
@@ -0,0 +1,16 @@
+#! /bin/sh
+set -e
+if [ -x "/etc/init.d/vdr${SPECIAL_VDR_SUFFIX}" ]; then
+    /etc/init.d/vdr${SPECIAL_VDR_SUFFIX} stop
+fi
+if [ "$1" = "remove" ] ; then
+    if [ -x "/etc/init.d/vdr${SPECIAL_VDR_SUFFIX}" ]; then
+        update-rc.d -f vdr${SPECIAL_VDR_SUFFIX} remove >/dev/null
+    fi
+    if [ -x "/etc/init.d/vdr" ]; then
+        /etc/init.d/vdr stop
+        update-rc.d vdr defaults >/dev/null
+        /etc/init.d/vdr start
+    fi
+fi
+#DEBHELPER#
--- debian/vdr${SPECIAL_VDR_SUFFIX}.templates  1970-01-01 00:00:00.000000000 +0000
+++ debian/vdr${SPECIAL_VDR_SUFFIX}.templates
@@ -0,0 +1,7 @@
+Template: vdr${SPECIAL_VDR_SUFFIX}/autostart
+Type: boolean
+Default: false
+Description: Start vdr${SPECIAL_VDR_SUFFIX} automatically instead of vdr?
+ On system startup either vdr${SPECIAL_VDR_SUFFIX} or vdr will be started in daemon mode.
+ By selecting this, you choose vdr${SPECIAL_VDR_SUFFIX} instead of vdr.
+ But you can manually switch between vdr${SPECIAL_VDR_SUFFIX} and vdr by menu commands.
EOF
    else
        /bin/sed -e "s/\${SPECIAL_VDR_SUFFIX}/${SPECIAL_VDR_SUFFIX}/g" <<'EOF' | /usr/bin/patch -p0 -F0
--- debian/rules
+++ debian/rules
@@ -123 +123 @@
-	dh_installinit -a
+	dh_installinit -a --noscripts
--- debian/vdr${SPECIAL_VDR_SUFFIX}.config
+++ debian/vdr${SPECIAL_VDR_SUFFIX}.config
@@ -24,1 +24,3 @@
-exit 0
+db_input high vdr${SPECIAL_VDR_SUFFIX}/autostart || true
+db_go || true
+exit 0
--- debian/vdr${SPECIAL_VDR_SUFFIX}.postinst
+++ debian/vdr${SPECIAL_VDR_SUFFIX}.postinst
@@ -1,1 +1,21 @@
-#! /bin/sh
+#! /bin/sh
+. /usr/share/debconf/confmodule
+if [ "$1" = "configure" ]; then
+    db_get vdr${SPECIAL_VDR_SUFFIX}/autostart
+    if [ "$RET" = "true" ]; then
+        START="vdr${SPECIAL_VDR_SUFFIX}"
+        STOP="vdr"
+    else
+        START="vdr"
+        STOP="vdr${SPECIAL_VDR_SUFFIX}"
+    fi
+    if [ -x "/etc/init.d/$STOP" ]; then
+        /etc/init.d/$STOP stop
+        update-rc.d -f $STOP remove >/dev/null
+    fi
+    if [ -x "/etc/init.d/$START" ]; then
+        /etc/init.d/$START stop
+        update-rc.d $START defaults >/dev/null
+        /etc/init.d/$START start
+    fi
+fi
--- debian/vdr${SPECIAL_VDR_SUFFIX}.prerm
+++ debian/vdr${SPECIAL_VDR_SUFFIX}.prerm
@@ -1,1 +1,15 @@
-#! /bin/sh
+#! /bin/sh
+set -e
+if [ -x "/etc/init.d/vdr${SPECIAL_VDR_SUFFIX}" ]; then
+    /etc/init.d/vdr${SPECIAL_VDR_SUFFIX} stop
+fi
+if [ "$1" = "remove" ] ; then
+    if [ -x "/etc/init.d/vdr${SPECIAL_VDR_SUFFIX}" ]; then
+        update-rc.d -f vdr${SPECIAL_VDR_SUFFIX} remove >/dev/null
+    fi
+    if [ -x "/etc/init.d/vdr" ]; then
+        /etc/init.d/vdr stop
+        update-rc.d vdr defaults >/dev/null
+        /etc/init.d/vdr start
+    fi
+fi
--- debian/vdr${SPECIAL_VDR_SUFFIX}.templates
+++ debian/vdr${SPECIAL_VDR_SUFFIX}.templates
@@ -0,0 +1,8 @@
+Template: vdr${SPECIAL_VDR_SUFFIX}/autostart
+Type: boolean
+Default: false
+Description: Start vdr${SPECIAL_VDR_SUFFIX} automatically instead of vdr?
+ On system startup either vdr${SPECIAL_VDR_SUFFIX} or vdr will be started in daemon mode.
+ By selecting this, you choose vdr${SPECIAL_VDR_SUFFIX} instead of vdr.
+ But you can manually switch between vdr${SPECIAL_VDR_SUFFIX} and vdr by menu commands.
+
EOF
    fi

    # Add commands to switch between the vdr variations
    /bin/sed -e "s/\${SPECIAL_VDR_SUFFIX}/${SPECIAL_VDR_SUFFIX}/g" <<'EOF' | /usr/bin/patch -p0 -F0
--- debian/commands.switch-vdr.conf  1970-01-01 00:00:00.000000000 +0000
+++ debian/commands.switch-vdr.conf
@@ -0,0 +1 @@
+VDR-Standardversion starten : nohup sh -c "( /usr/lib/vdr${SPECIAL_VDR_SUFFIX}/ctvdr${SPECIAL_VDR_SUFFIX}wrapper --stop && /bin/sleep 5 && /usr/lib/vdr/ctvdrwrapper --restart )" >/dev/null 2>&1 &
--- debian/commands.switch-vdr${SPECIAL_VDR_SUFFIX}.conf  1970-01-01 00:00:00.000000000 +0000
+++ debian/commands.switch-vdr${SPECIAL_VDR_SUFFIX}.conf
@@ -0,0 +1 @@
+VDR-Entwicklerversion vdr${SPECIAL_VDR_SUFFIX} starten : nohup sh -c "( /usr/lib/vdr/ctvdrwrapper --stop && /bin/sleep 5 && /usr/lib/vdr${SPECIAL_VDR_SUFFIX}/ctvdr${SPECIAL_VDR_SUFFIX}wrapper --restart )" >/dev/null 2>&1 &
--- debian/vdr${SPECIAL_VDR_SUFFIX}.install
+++ debian/vdr${SPECIAL_VDR_SUFFIX}.install
@@ -0,0 +1,2 @@
+debian/commands.switch-vdr.conf         usr/share/vdr${SPECIAL_VDR_SUFFIX}/command-hooks/
+debian/commands.switch-vdr${SPECIAL_VDR_SUFFIX}.conf         usr/share/vdr/command-hooks/
EOF

    echo "prepare_vdr: set links to use logos and addons from standard packages"
    /bin/sed -e "s/\${SPECIAL_VDR_SUFFIX}/${SPECIAL_VDR_SUFFIX}/g" <<'EOF' | /usr/bin/patch -p0 -F0
--- debian/commands-loader.sh
+++ debian/commands-loader.sh
@@ -33 +33 @@
-    cmds=( `find $CMDHOOKSDIR -maxdepth 1 -name "$cmdtype.*.conf" -printf "%f \n" | sed "s/$cmdtype\.\(.\+\)\.conf/\1/g"` )
+    cmds=( `find $CMDHOOKSDIR -maxdepth 1 -name "$cmdtype.*.conf" -xtype f -printf "%f \n" | sed "s/$cmdtype\.\(.\+\)\.conf/\1/g"` )
--- debian/vdr${SPECIAL_VDR_SUFFIX}.links
+++ debian/vdr${SPECIAL_VDR_SUFFIX}.links
@@ -0,0 +1,21 @@
+var/lib/vdr/logos                                         var/lib/vdr${SPECIAL_VDR_SUFFIX}/logos
+usr/share/vdr/shutdown-hooks/S90.acpiwakeup               usr/share/vdr${SPECIAL_VDR_SUFFIX}/shutdown-hooks/S90.acpiwakeup
+usr/share/vdr/command-hooks/reccmds.noad.conf             usr/share/vdr${SPECIAL_VDR_SUFFIX}/command-hooks/reccmds.noad.conf
+usr/share/vdr/recording-hooks/R10.noad                    usr/share/vdr${SPECIAL_VDR_SUFFIX}/recording-hooks/R10.noad
+usr/share/vdr/shutdown-hooks/S50.noad                     usr/share/vdr${SPECIAL_VDR_SUFFIX}/shutdown-hooks/S50.noad
+usr/share/vdr/shutdown-hooks/S90.nvram-wakeup             usr/share/vdr${SPECIAL_VDR_SUFFIX}/shutdown-hooks/S90.nvram-wakeup
+usr/share/vdr/recording-hooks/R10.sharemarks              usr/share/vdr${SPECIAL_VDR_SUFFIX}/recording-hooks/R10.sharemarks
+usr/share/vdr/command-hooks/reccmds.sharemarks.conf       usr/share/vdr${SPECIAL_VDR_SUFFIX}/command-hooks/reccmds.sharemarks.conf
+usr/share/vdr/command-hooks/reccmds.tosvcd.conf           usr/share/vdr${SPECIAL_VDR_SUFFIX}/command-hooks/reccmds.tosvcd.conf
+usr/share/vdr/shutdown-hooks/S50.tosvcd                   usr/share/vdr${SPECIAL_VDR_SUFFIX}/shutdown-hooks/S50.tosvcd
+usr/share/vdr/command-hooks/commands.tvinfomerk2vdr.conf  usr/share/vdr${SPECIAL_VDR_SUFFIX}/command-hooks/commands.tvinfomerk2vdr.conf
+usr/share/vdr/command-hooks/commands.tvmovie2vdr.conf     usr/share/vdr${SPECIAL_VDR_SUFFIX}/command-hooks/commands.tvmovie2vdr.conf
+usr/share/vdr/command-hooks/commands.vdrconvert.conf      usr/share/vdr${SPECIAL_VDR_SUFFIX}/command-hooks/commands.vdrconvert.conf
+usr/share/vdr/command-hooks/reccmds.vdrconvert-divx.conf  usr/share/vdr${SPECIAL_VDR_SUFFIX}/command-hooks/reccmds.vdrconvert-divx.conf
+usr/share/vdr/command-hooks/reccmds.vdrconvert-svcd.conf  usr/share/vdr${SPECIAL_VDR_SUFFIX}/command-hooks/reccmds.vdrconvert-svcd.conf
+usr/share/vdr/command-hooks/reccmds.vdrconvert-vcd.conf   usr/share/vdr${SPECIAL_VDR_SUFFIX}/command-hooks/reccmds.vdrconvert-vcd.conf
+usr/share/vdr/command-hooks/reccmds.vdrconvert-mpeg.conf  usr/share/vdr${SPECIAL_VDR_SUFFIX}/command-hooks/reccmds.vdrconvert-mpeg.conf
+usr/share/vdr/command-hooks/reccmds.vdrconvert-mp3.conf   usr/share/vdr${SPECIAL_VDR_SUFFIX}/command-hooks/reccmds.vdrconvert-mp3.conf
+usr/share/vdr/command-hooks/reccmds.vdrconvert-ac3.conf   usr/share/vdr${SPECIAL_VDR_SUFFIX}/command-hooks/reccmds.vdrconvert-ac3.conf
+usr/share/vdr/command-hooks/reccmds.vdrconvert-dvd.conf   usr/share/vdr${SPECIAL_VDR_SUFFIX}/command-hooks/reccmds.vdrconvert-dvd.conf
+usr/share/vdr/shutdown-hooks/S50.vdrconvert               usr/share/vdr${SPECIAL_VDR_SUFFIX}/shutdown-hooks/S50.vdrconvert
EOF

    echo "prepare_vdr: add hint to the plugin debianizer script"
    /bin/sed -e "s/\${SPECIAL_VDR_SUFFIX}/${SPECIAL_VDR_SUFFIX}/g" <<'EOF' >> "debian/debianize-vdr${SPECIAL_VDR_SUFFIX}plugin"
echo
echo "To build vdr${SPECIAL_VDR_SUFFIX} plugin packages use the environment variable"
echo "SPECIAL_VDR_SUFFIX, e.g.:"
echo "    SPECIAL_VDR_SUFFIX=${SPECIAL_VDR_SUFFIX} fakeroot dpkg-buildpackage -us -uc -tc"
echo "See /usr/share/vdr-dev/make-special-vdr.sh for details."
EOF
    /bin/sed -e "s/\${SPECIAL_VDR_SUFFIX}/${SPECIAL_VDR_SUFFIX}/g" <<'EOF' | /usr/bin/patch -p0 -F0
--- debian/debianize-vdr${SPECIAL_VDR_SUFFIX}plugin
+++ debian/debianize-vdr${SPECIAL_VDR_SUFFIX}plugin
@@ -15 +15 @@
-        echo "The upsteam tarball should be named: vdr${SPECIAL_VDR_SUFFIX}-<PLUGIN-NAME>-<VERSION>.tar.gz"
+        echo "The upsteam tarball should be named: vdr-<PLUGIN-NAME>-<VERSION>.tar.gz"
@@ -17 +17 @@
-        echo "e.g.: vdr${SPECIAL_VDR_SUFFIX}-coolplugin-0.0.1.tar.gz"
+        echo "e.g.: vdr-coolplugin-0.0.1.tar.gz"
@@ -27 +27 @@
-    ORIGTARBALL="../vdr${SPECIAL_VDR_SUFFIX}-plugin-$PLUGIN"_"$VERSION.orig.tar.gz"
+    ORIGTARBALL="../vdr-plugin-$PLUGIN"_"$VERSION.orig.tar.gz"
@@ -54 +54 @@
-    VDRVERSION=`dpkg -s vdr${SPECIAL_VDR_SUFFIX}-dev | awk '/Version/ { print $2 }'`
+    VDRVERSION=`dpkg -s vdr-dev | awk '/Version/ { print $2 }'`
@@ -72 +72 @@
-dh_make="/usr/bin/dh_make -t /usr/share/vdr${SPECIAL_VDR_SUFFIX}-dev/plugin-template -b -p vdr${SPECIAL_VDR_SUFFIX}-plugin-$PLUGIN"
+dh_make="/usr/bin/dh_make -t /usr/share/vdr${SPECIAL_VDR_SUFFIX}-dev/plugin-template -b -p vdr-plugin-$PLUGIN"
EOF
}

prepare_analogtv()
{
    echo "prepare_analogtv: rename mp1e -> mp1e_vdr${SPECIAL_VDR_SUFFIX}"
    /bin/sed -e "s/\${SPECIAL_VDR_SUFFIX}/${SPECIAL_VDR_SUFFIX}/g" <<'EOF' | /usr/bin/patch -p0 -F0
--- player-analogtv.c
+++ player-analogtv.c
@@ -569 +569 @@
-                     sprintf(cmd, "mp1e -m %d %s%s%s%s -t %d -g %s -p %s -c %s -x %s -d %d -a %d -b %d -B %d%s -r %d,%d -s %s -S %2.1f -F %d%s%s%s%s -o %s &",
+                     sprintf(cmd, "mp1e_vdr${SPECIAL_VDR_SUFFIX} -m %d %s%s%s%s -t %d -g %s -p %s -c %s -x %s -d %d -a %d -b %d -B %d%s -r %d,%d -s %s -S %2.1f -F %d%s%s%s%s -o %s &",
--- debian/rules
+++ debian/rules
@@ -73,1 +73,3 @@
-	dh_install
+	dh_install
+	cd debian/vdr${SPECIAL_VDR_SUFFIX}-plugin-analogtv/usr/bin; mv mp1e mp1e_vdr${SPECIAL_VDR_SUFFIX}
+	cd debian/vdr${SPECIAL_VDR_SUFFIX}-plugin-analogtv/usr/share/man/man1; mv mp1e.1 mp1e_vdr${SPECIAL_VDR_SUFFIX}.1
EOF
}

prepare_burn()
{
    echo "prepare_burn: use backgrounds from standard packages"
    /bin/sed -e "s/\${SPECIAL_VDR_SUFFIX}/${SPECIAL_VDR_SUFFIX}/g" <<'EOF' | /usr/bin/patch -p0 -F0
--- debian/control
+++ debian/control
@@ -13,2 +13,2 @@
-Suggests: vdr${SPECIAL_VDR_SUFFIX}-burnbackgrounds (>= 0.0.1-4)
-Conflicts: vdr${SPECIAL_VDR_SUFFIX}-burnbackgrounds (<= 0.0.1-3)
+Suggests: vdr-burnbackgrounds (>= 0.0.1-4)
+Conflicts: vdr-burnbackgrounds (<= 0.0.1-3)
--- debian/links
+++ debian/links
@@ -0,0 +1,6 @@
+var/lib/vdr/plugins/burn/skins/fo-doku    var/lib/vdr${SPECIAL_VDR_SUFFIX}/plugins/burn/skins/fo-doku
+var/lib/vdr/plugins/burn/skins/fo-kinder  var/lib/vdr${SPECIAL_VDR_SUFFIX}/plugins/burn/skins/fo-kinder
+var/lib/vdr/plugins/burn/skins/fo-kino    var/lib/vdr${SPECIAL_VDR_SUFFIX}/plugins/burn/skins/fo-kino
+var/lib/vdr/plugins/burn/skins/fo-musik   var/lib/vdr${SPECIAL_VDR_SUFFIX}/plugins/burn/skins/fo-musik
+var/lib/vdr/plugins/burn/skins/fo-natur   var/lib/vdr${SPECIAL_VDR_SUFFIX}/plugins/burn/skins/fo-natur
+var/lib/vdr/plugins/burn/skins/fo-sport   var/lib/vdr${SPECIAL_VDR_SUFFIX}/plugins/burn/skins/fo-sport
EOF
}

prepare_graphtft()
{
    echo "prepare_graphtft: correct device option vdr"
    /bin/sed -e "s/\${SPECIAL_VDR_SUFFIX}/${SPECIAL_VDR_SUFFIX}/g" <<'EOF' | /usr/bin/patch -p0 -F0
--- display.c
+++ display.c
@@ -165 +165 @@
-      else if ((pos = strstr(dev, "vdr${SPECIAL_VDR_SUFFIX}/")))
+      else if ((pos = strstr(dev, "vdr/")))
--- graphtft.c
+++ graphtft.c
@@ -118 +118 @@
-      "                           /dev/fb0 or vdr${SPECIAL_VDR_SUFFIX}/1 \n"
+      "                           /dev/fb0 or vdr/1 \n"
@@ -202,2 +202,2 @@
-            fprintf(stderr, "vdr${SPECIAL_VDR_SUFFIX}: graphtft -  try device: vdr${SPECIAL_VDR_SUFFIX}/%d !\n", i);
-            asprintf(&_dev, "vdr${SPECIAL_VDR_SUFFIX}/%d", i);
+            fprintf(stderr, "vdr${SPECIAL_VDR_SUFFIX}: graphtft -  try device: vdr/%d !\n", i);
+            asprintf(&_dev, "vdr/%d", i);
--- debian/plugin.graphtft.conf
+++ debian/plugin.graphtft.conf
@@ -5 +5 @@
-#-d vdr${SPECIAL_VDR_SUFFIX}/1
+#-d vdr/1
EOF

    echo "prepare_graphtft: use skins from standard packages"
    /bin/sed -e "s/\${SPECIAL_VDR_SUFFIX}/${SPECIAL_VDR_SUFFIX}/g" <<'EOF' | /usr/bin/patch -p0 -F0
--- debian/control
+++ debian/control
@@ -12 +12 @@
-Depends: ${shlibs:Depends}, ${vdr${SPECIAL_VDR_SUFFIX}:Depends}, vdr${SPECIAL_VDR_SUFFIX}-tft-standard, ttf-bitstream-vera
+Depends: ${shlibs:Depends}, ${vdr${SPECIAL_VDR_SUFFIX}:Depends}, vdr-tft-standard, ttf-bitstream-vera
--- debian/dirs
+++ debian/dirs
@@ -1 +0,0 @@
-var/lib/vdr${SPECIAL_VDR_SUFFIX}/plugins/graphTFT/themes
--- debian/links
+++ debian/links
@@ -0,0 +1 @@
+var/lib/vdr/plugins/graphTFT/themes  var/lib/vdr${SPECIAL_VDR_SUFFIX}/plugins/graphTFT/themes
EOF
}

prepare_mediamvp()
{
    echo "prepare_mediamvp: rename mvploader -> mvploader_vdr${SPECIAL_VDR_SUFFIX}"
    SUBST="s.mvploader.mvploader_vdr${SPECIAL_VDR_SUFFIX}.g; \
           s./usr/lib/mediamvp./usr/share/vdr${SPECIAL_VDR_SUFFIX}-plugin-mediamvp.g"
    FILES=$(/usr/bin/find ./ -type f -not -regex "./${SAVE_DIR}/.*" \
                          -not -regex "./debian/changelog")
    subst_in_files "${SUBST}" ${FILES}
    rename_files   "${SUBST}" ${FILES}
    /bin/mv "debian/mvploader" "debian/mvploader_vdr${SPECIAL_VDR_SUFFIX}"
}

prepare_osdteletext()
{
    echo "prepare_osdteletext: correct 02_tmp-path-fix.dpatch"
    /bin/sed -e "s/\${SPECIAL_VDR_SUFFIX}/${SPECIAL_VDR_SUFFIX}/g" <<'EOF' | /usr/bin/patch -p0 -F0
--- debian/patches/02_tmp-path-fix.dpatch
+++ debian/patches/02_tmp-path-fix.dpatch
@@ -43 +43 @@
-+                                  (standard value: /var/cache/vdr${SPECIAL_VDR_SUFFIX}/vtx,
++                                  (standard value: /var/cache/vdr/vtx,
@@ -45 +45 @@
-                                    or /var/cache/vdr${SPECIAL_VDR_SUFFIX}/osdteletext.)
+                                    or /var/cache/vdr/osdteletext.)
@@ -56 +56 @@
-+                                  (Voreinstellung: /var/cache/vdr${SPECIAL_VDR_SUFFIX}/vtx,
++                                  (Voreinstellung: /var/cache/vdr/vtx,
@@ -58 +58 @@
-                                    oder /var/cache/vdr${SPECIAL_VDR_SUFFIX}/osdteletext.)
+                                    oder /var/cache/vdr/osdteletext.)
EOF
}

prepare_pin()
{
    echo "prepare_pin: rename fskcheck -> fskcheck_vdr${SPECIAL_VDR_SUFFIX}"
    SUBST="s/fskcheck/fskcheck_vdr${SPECIAL_VDR_SUFFIX}/g"
    FILES=$(/usr/bin/find ./ -type f -not -regex "./${SAVE_DIR}/.*" \
                          -not -regex "./debian/changelog")
    subst_in_files "${SUBST}" ${FILES}
    rename_files   "${SUBST}" ${FILES}
}

prepare_rssreader()
{
    echo "prepare_rssreader: correct rss entry"
    /bin/sed -e "s/\${SPECIAL_VDR_SUFFIX}/${SPECIAL_VDR_SUFFIX}/g" <<'EOF' | /usr/bin/patch -p0 -F0
--- debian/rssreader.conf
+++ debian/rssreader.conf
@@ -15 +15 @@
-VDR Announcements   : http://www.netholic.com/extras/vdr${SPECIAL_VDR_SUFFIX}_announce_rss.php?num=10
+VDR Announcements   : http://www.netholic.com/extras/vdr_announce_rss.php?num=10
EOF
}

prepare_softdevice()
{
    echo "prepare_softdevice: ShmClient -> ShmClient_vdr${SPECIAL_VDR_SUFFIX}"
    if /usr/bin/dpkg --compare-versions "$(/usr/bin/dpkg-parsechangelog | /bin/egrep '^Version:' | /usr/bin/cut -f 2 -d ' ')" \
                                        ge "0.4.0+cvs20070830-8"; then
        /bin/sed -e "s/\${SPECIAL_VDR_SUFFIX}/${SPECIAL_VDR_SUFFIX}/g" <<'EOF' >> debian/rules
common-binary-post-install-arch::
	cd debian/vdr${SPECIAL_VDR_SUFFIX}-plugin-softdevice/usr/bin; mv ShmClient ShmClient_vdr${SPECIAL_VDR_SUFFIX}
EOF
    else
        /bin/sed -e "s/\${SPECIAL_VDR_SUFFIX}/${SPECIAL_VDR_SUFFIX}/g" <<'EOF' | /usr/bin/patch -p0 -F0
--- debian/rules
+++ debian/rules
@@ -54,1 +54,2 @@
-	dh_install
+	dh_install
+	cd debian/vdr${SPECIAL_VDR_SUFFIX}-plugin-softdevice/usr/bin; mv ShmClient ShmClient_vdr${SPECIAL_VDR_SUFFIX}
EOF
    fi
}

prepare_sudoku()
{
    echo "prepare_sudoku: rename sudoku_generator -> sudoku_generator_vdr${SPECIAL_VDR_SUFFIX}"
    if /usr/bin/dpkg --compare-versions "$(/usr/bin/dpkg-parsechangelog | /bin/egrep '^Version:' | /usr/bin/cut -f 2 -d ' ')" \
                                        ge "0.2.0-1"; then
        /bin/sed -e "s/\${SPECIAL_VDR_SUFFIX}/${SPECIAL_VDR_SUFFIX}/g" <<'EOF' >> debian/rules
common-binary-post-install-arch::
	cd debian/vdr${SPECIAL_VDR_SUFFIX}-plugin-sudoku/usr/bin; mv sudoku_generator sudoku_generator_vdr${SPECIAL_VDR_SUFFIX}
EOF
    else
        /bin/sed -e "s/\${SPECIAL_VDR_SUFFIX}/${SPECIAL_VDR_SUFFIX}/g" <<'EOF' | /usr/bin/patch -p0 -F0
--- debian/rules
+++ debian/rules
@@ -53,1 +53,2 @@
-	dh_install
+	dh_install
+	cd debian/vdr${SPECIAL_VDR_SUFFIX}-plugin-sudoku/usr/bin; mv sudoku_generator sudoku_generator_vdr${SPECIAL_VDR_SUFFIX}
EOF
    fi
}

prepare_text2skin()
{
    echo "prepare_text2skin: use skins from standard packages"
    /bin/sed -e "s/\${SPECIAL_VDR_SUFFIX}/${SPECIAL_VDR_SUFFIX}/g" <<'EOF' | /usr/bin/patch -p0 -F0
--- debian/control
+++ debian/control
@@ -15,1 +15,1 @@
-Suggests: vdr${SPECIAL_VDR_SUFFIX}-skins
+Suggests: vdr-skins
--- debian/links  1970-01-01 00:00:00.000000000 +0000
+++ debian/links
@@ -0,0 +1 @@
+var/lib/vdr/plugins/text2skin  var/lib/vdr${SPECIAL_VDR_SUFFIX}/plugins/text2skin
EOF

    if /usr/bin/dpkg --compare-versions "$(/usr/bin/dpkg-parsechangelog | /bin/egrep '^Version:' | /usr/bin/cut -f 2 -d ' ')" \
                                        ge "1.0+cvs20080122.2311-1"; then
        echo "prepare_text2skin: use skin locales from standard packages"
        /bin/sed -e "s/\${SPECIAL_VDR_SUFFIX}/${SPECIAL_VDR_SUFFIX}/g" <<'EOF' | /usr/bin/patch -p0 -F0
--- debian/patches/95_text2skin-1.1-cvs-locale.dpatch
+++ debian/patches/95_text2skin-1.1-cvs-locale.dpatch
@@ -185,2 +185,2 @@
-+	mIdentity   = std::string("vdr${SPECIAL_VDR_SUFFIX}-"PLUGIN_NAME_I18N"-") + Skin;
-+	I18nRegister(mIdentity.substr(mIdentity.find('-') + 1).c_str());
++	mIdentity   = std::string("vdr-"PLUGIN_NAME_I18N"-") + Skin;
++	extern char *bindtextdomain(const char *, const char *); bindtextdomain(mIdentity.c_str(), "/usr/share/locale");
EOF
    fi
}

prepare_vdrc()
{
    echo "prepare_vdrc: set conflict to old special package"
    /bin/sed -e "s/\${SPECIAL_VDR_SUFFIX}/${SPECIAL_VDR_SUFFIX}/g" <<'EOF' | /usr/bin/patch -p0 -F0
--- debian/control
+++ debian/control
@@ -12,1 +12,3 @@
-Depends: ${shlibs:Depends}, ${vdr${SPECIAL_VDR_SUFFIX}:Depends}
+Depends: ${shlibs:Depends}, ${vdr${SPECIAL_VDR_SUFFIX}:Depends}
+Conflicts: vdr${SPECIAL_VDR_SUFFIX}-plugin-vdrc
+Replaces: vdr${SPECIAL_VDR_SUFFIX}-plugin-vdrc
EOF
}

prepare_vdrcd()
{
    echo "prepare_vdrcd: set conflict to old special package"
    /bin/sed -e "s/\${SPECIAL_VDR_SUFFIX}/${SPECIAL_VDR_SUFFIX}/g" <<'EOF' | /usr/bin/patch -p0 -F0
--- debian/control
+++ debian/control
@@ -11,1 +11,3 @@
-Depends: ${shlibs:Depends}, ${vdr${SPECIAL_VDR_SUFFIX}:Depends}, vdr${SPECIAL_VDR_SUFFIX}-plugin-dvd, vdr${SPECIAL_VDR_SUFFIX}-plugin-vcd
+Depends: ${shlibs:Depends}, ${vdr${SPECIAL_VDR_SUFFIX}:Depends}, vdr${SPECIAL_VDR_SUFFIX}-plugin-dvd, vdr${SPECIAL_VDR_SUFFIX}-plugin-vcd
+Conflicts: vdr${SPECIAL_VDR_SUFFIX}-plugin-vdrcd
+Replaces: vdr${SPECIAL_VDR_SUFFIX}-plugin-vdrcd
EOF
}

prepare_vdrrip()
{
    echo "prepare_vdrrip: rename queuehandler.sh -> queuehandler_vdr${SPECIAL_VDR_SUFFIX}.sh"
    SUBST="s/queuehandler.sh/queuehandler_vdr${SPECIAL_VDR_SUFFIX}.sh/g"
    FILES=$(/usr/bin/find ./ -type f -not -regex "./${SAVE_DIR}/.*" \
                          -not -regex "./debian/changelog")
    subst_in_files "${SUBST}" ${FILES}
    rename_files   "${SUBST}" ${FILES}

    echo "prepare_vdrrip: set conflict to old special package"
    /bin/sed -e "s/\${SPECIAL_VDR_SUFFIX}/${SPECIAL_VDR_SUFFIX}/g" <<'EOF' | /usr/bin/patch -p0 -F0
--- debian/control
+++ debian/control
@@ -12,1 +12,3 @@
-Depends: ${shlibs:Depends}, ${misc:Depends}, ${vdr${SPECIAL_VDR_SUFFIX}:Depends}
+Depends: ${shlibs:Depends}, ${misc:Depends}, ${vdr${SPECIAL_VDR_SUFFIX}:Depends}
+Conflicts: vdr${SPECIAL_VDR_SUFFIX}-plugin-vdrrip
+Replaces: vdr${SPECIAL_VDR_SUFFIX}-plugin-vdrrip
EOF
}

prepare_vompserver()
{
    echo "prepare_vompserver: use vompclient from standard package"
    SUBST="s/vdr${SPECIAL_VDR_SUFFIX}-vompclient-mvp/vdr-vompclient-mvp/g"
    subst_in_files "${SUBST}" "debian/control"

    /bin/sed -e "s/\${SPECIAL_VDR_SUFFIX}/${SPECIAL_VDR_SUFFIX}/g" <<'EOF' | /usr/bin/patch -p0 -F0
--- debian/vomp.conf
+++ debian/vomp.conf
@@ -30 +30 @@
-TFTP directory = /usr/share/vdr${SPECIAL_VDR_SUFFIX}-plugin-vompserver
+TFTP directory = /usr/share/vdr-plugin-vompserver
EOF
}

prepare_wapd()
{
    echo "prepare_wapd: rename wappasswd -> wappasswd_vdr${SPECIAL_VDR_SUFFIX}"
    if /usr/bin/dpkg --compare-versions "$(/usr/bin/dpkg-parsechangelog | /bin/egrep '^Version:' | /usr/bin/cut -f 2 -d ' ')" \
                                        ge "0.9-1"; then
        /bin/sed -e "s/\${SPECIAL_VDR_SUFFIX}/${SPECIAL_VDR_SUFFIX}/g" <<'EOF' >> debian/rules
common-binary-post-install-arch::
	cd debian/vdr${SPECIAL_VDR_SUFFIX}-plugin-wapd/usr/bin; mv wappasswd wappasswd_vdr${SPECIAL_VDR_SUFFIX}
EOF
    else
        /bin/sed -e "s/\${SPECIAL_VDR_SUFFIX}/${SPECIAL_VDR_SUFFIX}/g" <<'EOF' | /usr/bin/patch -p0 -F0
--- debian/rules
+++ debian/rules
@@ -53,1 +53,2 @@
-	dh_install
+	dh_install
+	cd debian/vdr${SPECIAL_VDR_SUFFIX}-plugin-wapd/usr/bin; mv wappasswd wappasswd_vdr${SPECIAL_VDR_SUFFIX}
EOF
    fi
}

prepare_xine()
{
    echo "prepare_xine: rename xineplayer -> xineplayer_vdr${SPECIAL_VDR_SUFFIX}"
    /bin/sed -e "s/\${SPECIAL_VDR_SUFFIX}/${SPECIAL_VDR_SUFFIX}/g" <<'EOF' | /usr/bin/patch -p0 -F0
--- debian/rules
+++ debian/rules
@@ -69,1 +69,2 @@
-	dh_install
+	dh_install
+	cd debian/vdr${SPECIAL_VDR_SUFFIX}-plugin-xine/usr/bin; mv xineplayer xineplayer_vdr${SPECIAL_VDR_SUFFIX}
EOF
}

prepare_xineliboutput()
{
    echo "prepare_xineliboutput: use libxine/xineliboutput from standard package"
    SUBST="/Package: libxineliboutput-fbfe/,/^\$/d; \
           /Package: libxineliboutput-sxfe/,/^\$/d; \
           /Package: xineliboutput-fbfe/,/^\$/d; \
           /Package: xineliboutput-sxfe/,/^\$/d; \
           /Package: libxine-xvdr${SPECIAL_VDR_SUFFIX}/,/^\$/d"
    subst_in_files "${SUBST}" "debian/control"

    /bin/sed -e "s/\${VERSION}/$(grep 'static const char \*VERSION *=' xineliboutput.c | cut -d'"' -f2)/g" \
             -e "s/\${SPECIAL_VDR_SUFFIX}/${SPECIAL_VDR_SUFFIX}/g" <<'EOF' | /usr/bin/patch -p0 -F0
--- debian/vdr${SPECIAL_VDR_SUFFIX}-plugin-xineliboutput.links  1970-01-01 00:00:00.000000000 +0000
+++ debian/vdr${SPECIAL_VDR_SUFFIX}-plugin-xineliboutput.links
@@ -0,0 +1,2 @@
+usr/lib/vdr/plugins/libxineliboutput-fbfe.so.${VERSION}  usr/lib/vdr${SPECIAL_VDR_SUFFIX}/plugins/libxineliboutput-fbfe.so.${VERSION}
+usr/lib/vdr/plugins/libxineliboutput-sxfe.so.${VERSION}  usr/lib/vdr${SPECIAL_VDR_SUFFIX}/plugins/libxineliboutput-sxfe.so.${VERSION}
EOF
}

cleanup()
{
    if [ -e "${SAVE_DIR}" ]; then
        echo "cleanup: remove all but ${SAVE_DIR} and restore all files form there"
        /bin/rm -rf $(/usr/bin/find ./ -mindepth 1 -maxdepth 1 \
                                    -not -name "${SAVE_DIR}")
        cd "${SAVE_DIR}"
        /bin/cp -af $(/usr/bin/find ./ -mindepth 1 -maxdepth 1) ..
        cd ..
        /bin/rm -rf "${SAVE_DIR}"
    fi
}

check_clean_arg()
{
    for ARG; do
        [ "${ARG}" = "clean" ] && return
    done
    false
}

check_package()
{
    for ARG; do
        /usr/bin/dh_listpackages | /bin/grep -q "^${ARG}\$" && return
    done
    false
}

subst_in_files()
{
    SUBST="$1"
    shift
    for F; do
        /bin/chmod +w "${F}"
        /bin/cp -a -f "${F}" "${TMP_FILE}"
        /bin/sed -e "${SUBST}" "${F}" >"${TMP_FILE}"
        if ! /usr/bin/cmp -s "${F}" "${TMP_FILE}"; then
            /usr/bin/touch -r "${F}" "${TMP_FILE}"
            /bin/mv -f "${TMP_FILE}" "${F}"
        fi
    done
}

rename_files()
{
    SUBST="$1"
    shift
    for F; do
        N=$(dirname "${F}")/$(basename "${F}" | /bin/sed -e "${SUBST}")
        if [ "${F}" != "${N}" ]; then
            /bin/mv "${F}" "${N}"
        fi
    done
}

main "$@"
