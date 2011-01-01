#!/usr/bin/python

import re
import hashlib
import os
from optparse import OptionParser

PATCHES_FILE = 'debian/.vdr-patches'

def file_name_for_patch_variant(baseFileName):
    if options.patchVariant:
        return baseFileName + "." + options.patchVariant
    else:
        return baseFileName

def get_active_patches():
    active_patches = {}
    for line in open(file_name_for_patch_variant("debian/patches/00list"), "r"):
        match = re.match('^(?!00_)([^#]+)', line.rstrip())
        if match:
            patchFileName = "debian/patches/" + match.group(1)
            if not os.path.exists(patchFileName):
                patchFileName += ".dpatch"
            if os.path.exists(patchFileName):
                active_patches[patchFileName] = hashlib.md5(open(patchFileName).read()).hexdigest()
    return active_patches

def get_last_patches():
    lastPatches = {}
    for line in open(file_name_for_patch_variant(PATCHES_FILE), "r"):
        match = re.match('(.+):(.+)', line.rstrip())
        if match:
            lastPatches[match.group(1)] = match.group(2)
    return lastPatches

def update_patchlist():
    patchListFile = open(file_name_for_patch_variant(PATCHES_FILE), "w")
    patches = get_active_patches()
    for fileName in patches:
        patchListFile.write(fileName + ":" + patches[fileName] + "\n")

def report_patches(patches, reportText):
    if len(patches) > 0:
        print reportText
        for p in patches:
            print "    " + p
        print

def check_patches():
    active_patches = get_active_patches()
    last_patches = get_last_patches()

    new_patches = [p for p in active_patches if last_patches.keys().count(p) == 0]
    removed_patches = [p for p in last_patches if active_patches.keys().count(p) == 0]
    changed_patches = [p for p in last_patches if p in active_patches and active_patches[p] != last_patches[p]]

    report_patches(new_patches, "The following patches are new:")
    report_patches(removed_patches, "The following patches have been disabled:") 
    report_patches(changed_patches, "The following patches have been modified:")

    if len(new_patches) + len(removed_patches) + len(changed_patches) > 0:
        commandLine = "debian/rules accept-patches"
        abiVersion = "abi-version"
        if options.patchVariant:
            commandLine = "PATCHVARIANT=" + options.patchVariant + " " + commandLine
            abiVersion += "." + options.patchVariant
        print "Please check, if any of the above changes affects VDR's ABI!"
        print "If this is the case, then update %s and run" % abiVersion
        print "'%s' to update the snapshot of" % commandLine
        print "the current patch level."
        exit(1)

#
# main()
#

parser = OptionParser()

parser.add_option("-u", "--update", action="store_true", dest="doUpdate", help="updated the list of accepted patches")
parser.add_option("-c", "--check", action="store_true", dest="doCheck", help="check patches")
parser.add_option("-p", "--patchvariant", dest="patchVariant", help="use a patch variant")

(options, args) = parser.parse_args()

if options.doCheck:
    check_patches()
elif options.doUpdate:
    update_patchlist()
else:
    parser.print_help()
