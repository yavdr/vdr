use warnings;
use strict;
use Debian::Debhelper::Dh_Lib;

insert_after("dh_shlibdeps", "dh_vdrplugin_depends");

insert_after("dh_install", "dh_vdrplugin_enable");

insert_after("dh_vdrplugin_enable", "dh_vdrplugin_migrate");

1
