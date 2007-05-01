#!/bin/sh
#
# This script checks which groups the vdr user should belong to and adds
# it to the necessary groups or removes it from groups which are not needed
# anymore
#
# (c) 2007, Thomas Schmidt <tschmidt@debian.org>
#

DIR="/etc/vdr/groups.d"
VDR_USER=vdr

NEEDED_GROUPS=`cat $DIR/* | grep -v "^#\|^$" | sed s/"\(.*\)#.*"/"\1"/ | xargs`
ACTUAL_GROUPS=`groups $VDR_USER | cut -d' ' -f3-`

# add $VDR_USER to the required groups
for NEEDED_GROUP in $NEEDED_GROUPS; do
   REQUIRED=1

   for ACTUAL_GROUP in $ACTUAL_GROUPS; do
      if [ $NEEDED_GROUP = $ACTUAL_GROUP ]; then
         REQUIRED=0
      fi
   done

   if [ $REQUIRED = "1" ]; then
      # add $VDR_USER to $NEEDED_GROUP
      echo "Adding $VDR_USER to group $NEEDED_GROUP"
      adduser $VDR_USER $NEEDED_GROUP > /dev/null 2>&1
   fi
done

# check if $VDR_USER is member of any unnecessary groups
for ACTUAL_GROUP in $ACTUAL_GROUPS; do
   REQUIRED=0

   for NEEDED_GROUP in $NEEDED_GROUPS; do
      if [ $ACTUAL_GROUP = $NEEDED_GROUP ]; then
         REQUIRED=1
      fi
   done

   if [ $REQUIRED = "0" ]; then
      # remove $VDR_USER from $ACTUAL_GROUP
      echo "Removing $VDR_USER from group $ACTUAL_GROUP"
      deluser $VDR_USER $ACTUAL_GROUP > /dev/null 2>&1
   fi
done
