#
# Move settings from /etc/default/vdr to /etc/vdr/conf.d/00-vdr.conf
#

. /usr/lib/vdr/config-loader.sh

sed -e "s|\(--video\)=.*|\1=$VIDEO_DIR|g" \
    -e "s|\(--port\)=.*|\1=$SVDRP_PORT|g" \
    -e "s|\(--user\)=.*|\1=$USER|g" \
    -e "s|\(--config\)=.*|\1=$CFG_DIR|g" \
    -e "s|\(--lib\)=.*|\1=$PLUGIN_DIR|g" \
    -e "s|\(--record\)=.*|\1=$REC_CMD|g" \
    -e "s|\(--epfile\)=.*|\1=$EPG_FILE|g" \
    -e "s|.*\(--vfat\)|$([ "$VFAT" != "1" ] && echo '#')\1|g" \
    -e "s|.*\(--userdump\)|$([ "$ENABLE_CORE_DUMPS" != "1" ] && echo '#')\1|g" \
    -e "s|.*\(--shutdown\)|$([ "$ENABLE_SHUTDOWN" != "1" ] && echo '#')\1|g" \
    -e "s|.*--lirc.*|$([ -n "$LIRC" ] && echo "--lirc=$LIRC" || echo '#--lirc')|g" \
    -i /etc/vdr/conf.d/00-vdr.conf


if [ -n "$VDR_CHARSET_OVERRIDE" ]; then
  echo "--chartab=$VDR_CHARSET_OVERRIDE" >> /etc/vdr/conf.d/00-vdr.conf
fi
