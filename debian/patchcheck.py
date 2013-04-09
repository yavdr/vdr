#!/usr/bin/python

import re
import hashlib
import os
import subprocess
from textwrap import TextWrapper
from optparse import OptionParser

PATCHES_FILE = 'debian/.vdr-patches'
PATCH_INFO_FILE = 'debian/patchinfo'

def collect_patch_info():
    patchInfo = []
    for patch in subprocess.check_output('quilt series', shell=True).splitlines():
        md5 =  hashlib.md5(open('debian/patches/' + patch).read()).hexdigest()
        header = subprocess.check_output("quilt header '%s'" % patch, shell=True)
        author = description = None
        match = re.search('^Author: (.*)', header, re.MULTILINE)
        if match:
            author = match.group(1)
        match = re.search('^Description: ((.*?)\n( .*?\n)*)', header, re.DOTALL)
        if match:
            description = re.sub(r'^ \.?', '', match.group(1), 0, re.MULTILINE)
            description = re.sub(r'^([^.].*)\n', r'\1 ', description, 0, re.MULTILINE)
        if author and description:
             patchInfo.append((patch, md5, author, description))
        else:
            print 'Incomplete patch header in %s' % patch
            exit(1)
    return patchInfo

def get_last_patches():
    lastPatches = []
    for line in open(PATCHES_FILE, "r"):
        match = re.match('(.+):(.+)', line.rstrip())
        if match:
            lastPatches.append((match.group(1), match.group(2)))
    return lastPatches

def generate_patchlist(patchInfo):
    patchListFile = open(PATCHES_FILE, "w")
    for (fileName, md5, author, description) in patchInfo:
        print >>patchListFile, "%s:%s" %  (fileName, md5)

def generate_patchinfo(patchInfo):
    patchInfoFile = open(PATCH_INFO_FILE, "w")
    print >>patchInfoFile, 'Patches applied to vanilla vdr sources'
    print >>patchInfoFile, '--------------------------------------'
    print >>patchInfoFile
    for (fileName, md5, author, description) in patchInfo:
        print >>patchInfoFile, fileName
        print >>patchInfoFile, '    ' + author
        print >>patchInfoFile
        wrapper = TextWrapper(initial_indent='    ', subsequent_indent='    ', break_on_hyphens=False, width=80)
        for paragraph in description.splitlines():
            print >>patchInfoFile, wrapper.fill(paragraph)
            print >>patchInfoFile

def report_patches(patches, reportText):
    if len(patches) > 0:
        print reportText
        for p in patches:
            print "    " + p
        print

def check_patches():
    current_patches = [(p[0], p[1]) for p in collect_patch_info()]
    last_patches = get_last_patches()

    new_patches = set(p[0] for p in current_patches) - set(p[0] for p in last_patches)
    removed_patches = set(p[0] for p in last_patches) - set(p[0] for p in current_patches)
    changed_patches = set(p[0] for p in (set(last_patches) - set(current_patches))) - set(removed_patches)

    report_patches(new_patches, "The following patches are new:")
    report_patches(removed_patches, "The following patches have been disabled:")
    report_patches(changed_patches, "The following patches have been modified:")

    if len(new_patches) + len(removed_patches) + len(changed_patches) > 0:
        commandLine = "debian/rules accept-patches"
        abiVersion = "abi-version"
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

(options, args) = parser.parse_args()

if options.doCheck:
    check_patches()
elif options.doUpdate:
    patchInfo = collect_patch_info()
    generate_patchlist(patchInfo)
    generate_patchinfo(patchInfo)
else:
    parser.print_help()
