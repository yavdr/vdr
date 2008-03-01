#
# This file is called by /etc/init.d/vdr
#

#
# Defaults - don't touch, edit options for the VDR daemon in
# /etc/default/vdr !!!
#

# Config-Directory
CFG_DIR="/var/lib/vdr"

# Plugin-Directory
PLUGIN_DIR="/usr/lib/vdr/plugins"

# Plugin Config-Directory
PLUGIN_CFG_DIR="/etc/vdr/plugins"

# Plugin prefix
PLUGIN_PREFIX="libvdr-"

# Command-Hooks Directory
CMDHOOKSDIR="/usr/share/vdr/command-hooks"

# Commmand executed on start, stop and editing of a recording
REC_CMD=/usr/lib/vdr/vdr-recordingaction

# Commmand executed by vdr to shutdown the system
SHUTDOWNCMD="/sbin/shutdown -h now"

# EPG data file
EPG_FILE=/var/cache/vdr/epg.data

# Username under which vdr will run (Note: the user root is not 
# allowed to run vdr, vdr will abort when you try to start it as 
# root or with "-u root")
USER=vdr

# Default port for SVDRP
SVDRP_PORT=2001

# Enable / Disable vdr daemon
ENABLED=0

# Enable / Disable automatic shutdown
ENABLE_SHUTDOWN=0

# Video-Directory
VIDEO_DIR="/var/lib/video.00"

# Set this to load only plugins with the correct patch level
PLUGIN_CHECK_PATCHLEVEL="no"

# Set this to load only startable plugins (check with "vdr -V -P plugin")
PLUGIN_CHECK_STARTABLE="yes"

# Default Console for controlling VDR by keyboard. Empty means no console
# input.
KEYB_TTY=""

# Set this to 1 to make VDR switch to the console specified in KEYB_TTY
# on startup
KEYB_TTY_SWITCH=0

# get locale which is used for running vdr from /etc/environment, in case of 
# an error, use "C"
[ -e /etc/environment ] && . /etc/environment
[ -z "$LANG" ] && LANG="C"
VDR_LANG=$LANG

test -f /etc/default/vdr && . /etc/default/vdr
