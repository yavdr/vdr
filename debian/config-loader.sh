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
SVDRP_PORT=6419

# Enable / Disable vdr daemon
ENABLED=0

# Enable / Disable core dumps
ENABLE_CORE_DUMPS=0

# Enable / Disable automatic shutdown
ENABLE_SHUTDOWN=0

# Video-Directory
VIDEO_DIR="/srv/vdr/video"

# Set this to load only startable plugins (check with "vdr -V -P plugin")
PLUGIN_CHECK_STARTABLE="yes"

# Default Console for controlling VDR by keyboard. Empty means no console
# input.
KEYB_TTY=""

# Set this to 1 to make VDR switch to the console specified in KEYB_TTY
# on startup
KEYB_TTY_SWITCH=0

# get locale which is used for running vdr from /etc/default/locale or
# /etc/environment or fall back to "C"
ENV_FILE="none"
[ -r /etc/environment ] && ENV_FILE="/etc/environment"
[ -r /etc/default/locale ] && ENV_FILE="/etc/default/locale"
[ $ENV_FILE = none ] || \
  for var in LANG LC_ALL; do
    eval VDR_LANG=$(egrep "^[^#]*${var}=" $ENV_FILE | tail -n1 | cut -d= -f2)
    [ -z "$VDR_LANG" ] || break
  done
[ -z "$VDR_LANG" ] && VDR_LANG="C"

# Enable VFAT file system support by default
VFAT=1

# Default LIRC device
LIRC=/var/run/lirc/lircd

test -f /etc/default/vdr && . /etc/default/vdr
