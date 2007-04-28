#
# vdr-groups.sh  
# 
# Shell script to be used by vdr plugin packages  to register/deregister
# required vdr group memberships.
#
# Usage:
#
# /bin/sh /usr/share/vdr/vdr-groups.sh --add <GROUP> <PLUGIN-NAME>
# /bin/sh /usr/share/vdr/vdr-groups.sh --remove <GROUP> <PLUGIN-NAME>
#

VDRGROUPS=/etc/vdr/vdr-groups.sh

add_to_group()
{
   # 1. Add vdr to <GROUP>
   # 2. Add entry to vdr-groups.conf: 
   #    <GROUP>    # <PLUGIN-NAME> (don't touch this - will be maintained by the plugin)
}

remove_from_group()
{
   # 1. Remove mathching <GROUP> entry from vdr-groups.conf
   # 2. If no <GROUP> entry is left, remove user vdr from <GROUP>
}

