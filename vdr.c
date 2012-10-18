/*
 * vdr.c: Video Disk Recorder main program
 *
 * Copyright (C) 2000, 2003, 2006, 2008 Klaus Schmidinger
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 * Or, point your browser to http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 *
 * The author can be reached at kls@tvdr.de
 *
 * The project's page is at http://www.tvdr.de
 *
 * $Id: vdr.c 2.36 2012/04/26 09:23:41 kls Exp $
 */

#include <getopt.h>
#include <grp.h>
#include <langinfo.h>
#include <locale.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/capability.h>
#include <sys/prctl.h>
#include <termios.h>
#include <unistd.h>
#include "audio.h"
#include "channels.h"
#include "config.h"
#include "cutter.h"
#include "device.h"
#include "diseqc.h"
#include "dvbdevice.h"
#include "eitscan.h"
#include "epg.h"
#include "i18n.h"
#include "interface.h"
#include "keys.h"
#include "libsi/si.h"
#include "lirc.h"
#include "menu.h"
#include "osdbase.h"
#include "plugin.h"
#include "recording.h"
#include "shutdown.h"
#include "skinclassic.h"
#include "skinlcars.h"
#include "skinsttng.h"
#include "sourceparams.h"
#include "sources.h"
#include "themes.h"
#include "timers.h"
#include "tools.h"
#include "transfer.h"
#include "videodir.h"

#define MINCHANNELWAIT        10 // seconds to wait between failed channel switchings
#define ACTIVITYTIMEOUT       60 // seconds before starting housekeeping
#define SHUTDOWNWAIT         300 // seconds to wait in user prompt before automatic shutdown
#define SHUTDOWNRETRY        360 // seconds before trying again to shut down
#define SHUTDOWNFORCEPROMPT    5 // seconds to wait in user prompt to allow forcing shutdown
#define SHUTDOWNCANCELPROMPT   5 // seconds to wait in user prompt to allow canceling shutdown
#define RESTARTCANCELPROMPT    5 // seconds to wait in user prompt before restarting on SIGHUP
#define MANUALSTART          600 // seconds the next timer must be in the future to assume manual start
#define CHANNELSAVEDELTA     600 // seconds before saving channels.conf after automatic modifications
#define DEVICEREADYTIMEOUT    30 // seconds to wait until all devices are ready
#define MENUTIMEOUT          120 // seconds of user inactivity after which an OSD display is closed
#define TIMERCHECKDELTA       10 // seconds between checks for timers that need to see their channel
#define TIMERDEVICETIMEOUT     8 // seconds before a device used for timer check may be reused
#define TIMERLOOKAHEADTIME    60 // seconds before a non-VPS timer starts and the channel is switched if possible
#define VPSLOOKAHEADTIME      24 // hours within which VPS timers will make sure their events are up to date
#define VPSUPTODATETIME     3600 // seconds before the event or schedule of a VPS timer needs to be refreshed

#define EXIT(v) { ShutdownHandler.Exit(v); goto Exit; }

static int LastSignal = 0;

static bool SetUser(const char *UserName, bool UserDump)//XXX name?
{
  if (UserName) {
     struct passwd *user = getpwnam(UserName);
     if (!user) {
        fprintf(stderr, "vdr: unknown user: '%s'\n", UserName);
        return false;
        }
     if (setgid(user->pw_gid) < 0) {
        fprintf(stderr, "vdr: cannot set group id %u: %s\n", (unsigned int)user->pw_gid, strerror(errno));
        return false;
        }
     if (initgroups(user->pw_name, user->pw_gid) < 0) {
        fprintf(stderr, "vdr: cannot set supplemental group ids for user %s: %s\n", user->pw_name, strerror(errno));
        return false;
        }
     if (setuid(user->pw_uid) < 0) {
        fprintf(stderr, "vdr: cannot set user id %u: %s\n", (unsigned int)user->pw_uid, strerror(errno));
        return false;
        }
     if (UserDump && prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) < 0)
        fprintf(stderr, "vdr: warning - cannot set dumpable: %s\n", strerror(errno));
     }
  return true;
}

static bool DropCaps(void)
{
  // drop all capabilities except selected ones
  cap_t caps = cap_from_text("= cap_sys_nice,cap_sys_time,cap_net_raw=ep");
  if (!caps) {
     fprintf(stderr, "vdr: cap_from_text failed: %s\n", strerror(errno));
     return false;
     }
  if (cap_set_proc(caps) == -1) {
     fprintf(stderr, "vdr: cap_set_proc failed: %s\n", strerror(errno));
     cap_free(caps);
     return false;
     }
  cap_free(caps);
  return true;
}

static bool SetKeepCaps(bool On)
{
  // set keeping capabilities during setuid() on/off
  if (prctl(PR_SET_KEEPCAPS, On ? 1 : 0, 0, 0, 0) != 0) {
     fprintf(stderr, "vdr: prctl failed\n");
     return false;
     }
  return true;
}

static void SignalHandler(int signum)
{
  switch (signum) {
    case SIGPIPE:
         break;
    case SIGHUP:
         LastSignal = signum;
         break;
    default:
         LastSignal = signum;
         Interface->Interrupt();
         ShutdownHandler.Exit(0);
    }
  signal(signum, SignalHandler);
}

static void Watchdog(int signum)
{
  // Something terrible must have happened that prevented the 'alarm()' from
  // being called in time, so let's get out of here:
  esyslog("PANIC: watchdog timer expired - exiting!");
  exit(1);
}

int main(int argc, char *argv[])
{
  // Save terminal settings:

  struct termios savedTm;
  bool HasStdin = (tcgetpgrp(STDIN_FILENO) == getpid() || getppid() != (pid_t)1) && tcgetattr(STDIN_FILENO, &savedTm) == 0;

  // Initiate locale:

  setlocale(LC_ALL, "");
  setlocale(LC_NUMERIC, "C"); // makes sure any floating point numbers written use a decimal point

  // Command line options:

#define DEFAULTSVDRPPORT 6419
#define DEFAULTWATCHDOG     0 // seconds
#define DEFAULTCONFDIR CONFDIR
#define DEFAULTPLUGINDIR PLUGINDIR
#define DEFAULTEPGDATAFILENAME "epg.data"

  bool StartedAsRoot = false;
  const char *VdrUser = NULL;
  bool UserDump = false;
  int SVDRPport = DEFAULTSVDRPPORT;
  const char *AudioCommand = NULL;
  const char *ConfigDirectory = NULL;
  const char *EpgDataFileName = DEFAULTEPGDATAFILENAME;
  bool DisplayHelp = false;
  bool DisplayVersion = false;
  bool DaemonMode = false;
  int SysLogTarget = LOG_USER;
  bool MuteAudio = false;
  int WatchdogTimeout = DEFAULTWATCHDOG;
  const char *Terminal = NULL;
  const char *LocaleDir = NULL;

  bool UseKbd = true;
  const char *LircDevice = NULL;
#if !defined(REMOTE_KBD)
  UseKbd = false;
#endif
#if defined(REMOTE_LIRC)
  LircDevice = LIRC_DEVICE;
#endif
#if defined(VDR_USER)
  VdrUser = VDR_USER;
#endif

  cPluginManager PluginManager(DEFAULTPLUGINDIR);

  static struct option long_options[] = {
      { "audio",    required_argument, NULL, 'a' },
      { "config",   required_argument, NULL, 'c' },
      { "daemon",   no_argument,       NULL, 'd' },
      { "device",   required_argument, NULL, 'D' },
      { "edit",     required_argument, NULL, 'e' | 0x100 },
      { "epgfile",  required_argument, NULL, 'E' },
      { "filesize", required_argument, NULL, 'f' | 0x100 },
      { "genindex", required_argument, NULL, 'g' | 0x100 },
      { "grab",     required_argument, NULL, 'g' },
      { "help",     no_argument,       NULL, 'h' },
      { "instance", required_argument, NULL, 'i' },
      { "lib",      required_argument, NULL, 'L' },
      { "lirc",     optional_argument, NULL, 'l' | 0x100 },
      { "localedir",required_argument, NULL, 'l' | 0x200 },
      { "log",      required_argument, NULL, 'l' },
      { "mute",     no_argument,       NULL, 'm' },
      { "no-kbd",   no_argument,       NULL, 'n' | 0x100 },
      { "plugin",   required_argument, NULL, 'P' },
      { "port",     required_argument, NULL, 'p' },
      { "record",   required_argument, NULL, 'r' },
      { "shutdown", required_argument, NULL, 's' },
      { "split",    no_argument,       NULL, 's' | 0x100 },
      { "terminal", required_argument, NULL, 't' },
      { "user",     required_argument, NULL, 'u' },
      { "userdump", no_argument,       NULL, 'u' | 0x100 },
      { "version",  no_argument,       NULL, 'V' },
      { "vfat",     no_argument,       NULL, 'v' | 0x100 },
      { "video",    required_argument, NULL, 'v' },
      { "watchdog", required_argument, NULL, 'w' },
      { NULL,       no_argument,       NULL,  0  }
    };

  int c;
  while ((c = getopt_long(argc, argv, "a:c:dD:e:E:g:hi:l:L:mp:P:r:s:t:u:v:Vw:", long_options, NULL)) != -1) {
        switch (c) {
          case 'a': AudioCommand = optarg;
                    break;
          case 'c': ConfigDirectory = optarg;
                    break;
          case 'd': DaemonMode = true; break;
          case 'D': if (isnumber(optarg)) {
                       int n = atoi(optarg);
                       if (0 <= n && n < MAXDEVICES) {
                          cDevice::SetUseDevice(n);
                          break;
                          }
                       }
                    fprintf(stderr, "vdr: invalid DVB device number: %s\n", optarg);
                    return 2;
                    break;
          case 'e' | 0x100:
                    return CutRecording(optarg) ? 0 : 2;
          case 'E': EpgDataFileName = (*optarg != '-' ? optarg : NULL);
                    break;
          case 'f' | 0x100:
                    Setup.MaxVideoFileSize = StrToNum(optarg) / MEGABYTE(1);
                    if (Setup.MaxVideoFileSize < MINVIDEOFILESIZE)
                       Setup.MaxVideoFileSize = MINVIDEOFILESIZE;
                    if (Setup.MaxVideoFileSize > MAXVIDEOFILESIZETS)
                       Setup.MaxVideoFileSize = MAXVIDEOFILESIZETS;
                    break;
          case 'g' | 0x100:
                    return GenerateIndex(optarg) ? 0 : 2;
          case 'g': cSVDRP::SetGrabImageDir(*optarg != '-' ? optarg : NULL);
                    break;
          case 'h': DisplayHelp = true;
                    break;
          case 'i': if (isnumber(optarg)) {
                       InstanceId = atoi(optarg);
                       if (InstanceId >= 0)
                          break;
                       }
                    fprintf(stderr, "vdr: invalid instance id: %s\n", optarg);
                    return 2;
          case 'l': {
                      char *p = strchr(optarg, '.');
                      if (p)
                         *p = 0;
                      if (isnumber(optarg)) {
                         int l = atoi(optarg);
                         if (0 <= l && l <= 3) {
                            SysLogLevel = l;
                            if (!p)
                               break;
                            if (isnumber(p + 1)) {
                               int l = atoi(p + 1);
                               if (0 <= l && l <= 7) {
                                  int targets[] = { LOG_LOCAL0, LOG_LOCAL1, LOG_LOCAL2, LOG_LOCAL3, LOG_LOCAL4, LOG_LOCAL5, LOG_LOCAL6, LOG_LOCAL7 };
                                  SysLogTarget = targets[l];
                                  break;
                                  }
                               }
                            }
                         }
                    if (p)
                       *p = '.';
                    fprintf(stderr, "vdr: invalid log level: %s\n", optarg);
                    return 2;
                    }
                    break;
          case 'L': if (access(optarg, R_OK | X_OK) == 0)
                       PluginManager.SetDirectory(optarg);
                    else {
                       fprintf(stderr, "vdr: can't access plugin directory: %s\n", optarg);
                       return 2;
                       }
                    break;
          case 'l' | 0x100:
                    LircDevice = optarg ? optarg : LIRC_DEVICE;
                    break;
          case 'l' | 0x200:
                    if (access(optarg, R_OK | X_OK) == 0)
                       LocaleDir = optarg;
                    else {
                       fprintf(stderr, "vdr: can't access locale directory: %s\n", optarg);
                       return 2;
                       }
                    break;
          case 'm': MuteAudio = true;
                    break;
          case 'n' | 0x100:
                    UseKbd = false;
                    break;
          case 'p': if (isnumber(optarg))
                       SVDRPport = atoi(optarg);
                    else {
                       fprintf(stderr, "vdr: invalid port number: %s\n", optarg);
                       return 2;
                       }
                    break;
          case 'P': PluginManager.AddPlugin(optarg);
                    break;
          case 'r': cRecordingUserCommand::SetCommand(optarg);
                    break;
          case 's': ShutdownHandler.SetShutdownCommand(optarg);
                    break;
          case 's' | 0x100:
                    Setup.SplitEditedFiles = 1;
                    break;
          case 't': Terminal = optarg;
                    if (access(Terminal, R_OK | W_OK) < 0) {
                       fprintf(stderr, "vdr: can't access terminal: %s\n", Terminal);
                       return 2;
                       }
                    break;
          case 'u': if (*optarg)
                       VdrUser = optarg;
                    break;
          case 'u' | 0x100:
                    UserDump = true;
                    break;
          case 'V': DisplayVersion = true;
                    break;
          case 'v' | 0x100:
                    VfatFileSystem = true;
                    break;
          case 'v': VideoDirectory = optarg;
                    while (optarg && *optarg && optarg[strlen(optarg) - 1] == '/')
                          optarg[strlen(optarg) - 1] = 0;
                    break;
          case 'w': if (isnumber(optarg)) {
                       int t = atoi(optarg);
                       if (t >= 0) {
                          WatchdogTimeout = t;
                          break;
                          }
                       }
                    fprintf(stderr, "vdr: invalid watchdog timeout: %s\n", optarg);
                    return 2;
                    break;
          default:  return 2;
          }
        }

  // Set user id in case we were started as root:

  if (VdrUser && geteuid() == 0) {
     StartedAsRoot = true;
     if (strcmp(VdrUser, "root")) {
        if (!SetKeepCaps(true))
           return 2;
        if (!SetUser(VdrUser, UserDump))
           return 2;
        if (!SetKeepCaps(false))
           return 2;
        if (!DropCaps())
           return 2;
        }
     }

  // Help and version info:

  if (DisplayHelp || DisplayVersion) {
     if (!PluginManager.HasPlugins())
        PluginManager.AddPlugin("*"); // adds all available plugins
     PluginManager.LoadPlugins();
     if (DisplayHelp) {
        printf("Usage: vdr [OPTIONS]\n\n"          // for easier orientation, this is column 80|
               "  -a CMD,   --audio=CMD    send Dolby Digital audio to stdin of command CMD\n"
               "  -c DIR,   --config=DIR   read config files from DIR (default: %s)\n"
               "  -d,       --daemon       run in daemon mode\n"
               "  -D NUM,   --device=NUM   use only the given DVB device (NUM = 0, 1, 2...)\n"
               "                           there may be several -D options (default: all DVB\n"
               "                           devices will be used)\n"
               "            --edit=REC     cut recording REC and exit\n"
               "  -E FILE,  --epgfile=FILE write the EPG data into the given FILE (default is\n"
               "                           '%s' in the video directory)\n"
               "                           '-E-' disables this\n"
               "                           if FILE is a directory, the default EPG file will be\n"
               "                           created in that directory\n"
               "            --filesize=SIZE limit video files to SIZE bytes (default is %dM)\n"
               "                           only useful in conjunction with --edit\n"
               "            --genindex=REC generate index for recording REC and exit\n"
               "  -g DIR,   --grab=DIR     write images from the SVDRP command GRAB into the\n"
               "                           given DIR; DIR must be the full path name of an\n"
               "                           existing directory, without any \"..\", double '/'\n"
               "                           or symlinks (default: none, same as -g-)\n"
               "  -h,       --help         print this help and exit\n"
               "  -i ID,    --instance=ID  use ID as the id of this VDR instance (default: 0)\n"
               "  -l LEVEL, --log=LEVEL    set log level (default: 3)\n"
               "                           0 = no logging, 1 = errors only,\n"
               "                           2 = errors and info, 3 = errors, info and debug\n"
               "                           if logging should be done to LOG_LOCALn instead of\n"
               "                           LOG_USER, add '.n' to LEVEL, as in 3.7 (n=0..7)\n"
               "  -L DIR,   --lib=DIR      search for plugins in DIR (default is %s)\n"
               "            --lirc[=PATH]  use a LIRC remote control device, attached to PATH\n"
               "                           (default: %s)\n"
               "            --localedir=DIR search for locale files in DIR (default is\n"
               "                           %s)\n"
               "  -m,       --mute         mute audio of the primary DVB device at startup\n"
               "            --no-kbd       don't use the keyboard as an input device\n"
               "  -p PORT,  --port=PORT    use PORT for SVDRP (default: %d)\n"
               "                           0 turns off SVDRP\n"
               "  -P OPT,   --plugin=OPT   load a plugin defined by the given options\n"
               "  -r CMD,   --record=CMD   call CMD before and after a recording\n"
               "  -s CMD,   --shutdown=CMD call CMD to shutdown the computer\n"
               "            --split        split edited files at the editing marks (only\n"
               "                           useful in conjunction with --edit)\n"
               "  -t TTY,   --terminal=TTY controlling tty\n"
               "  -u USER,  --user=USER    run as user USER; only applicable if started as\n"
               "                           root\n"
               "            --userdump     allow coredumps if -u is given (debugging)\n"
               "  -v DIR,   --video=DIR    use DIR as video directory (default: %s)\n"
               "  -V,       --version      print version information and exit\n"
               "            --vfat         encode special characters in recording names to\n"
               "                           avoid problems with VFAT file systems\n"
               "  -w SEC,   --watchdog=SEC activate the watchdog timer with a timeout of SEC\n"
               "                           seconds (default: %d); '0' disables the watchdog\n"
               "\n",
               DEFAULTCONFDIR,
               DEFAULTEPGDATAFILENAME,
               MAXVIDEOFILESIZEDEFAULT,
               DEFAULTPLUGINDIR,
               LIRC_DEVICE,
               LOCDIR,
               DEFAULTSVDRPPORT,
               VideoDirectory,
               DEFAULTWATCHDOG
               );
        }
     if (DisplayVersion)
        printf("vdr (%s/%s) - The Video Disk Recorder\n", VDRVERSION, APIVERSION);
     if (PluginManager.HasPlugins()) {
        if (DisplayHelp)
           printf("Plugins: vdr -P\"name [OPTIONS]\"\n\n");
        for (int i = 0; ; i++) {
            cPlugin *p = PluginManager.GetPlugin(i);
            if (p) {
               const char *help = p->CommandLineHelp();
               printf("%s (%s) - %s\n", p->Name(), p->Version(), p->Description());
               if (DisplayHelp && help) {
                  printf("\n");
                  puts(help);
                  }
               }
            else
               break;
            }
        }
     return 0;
     }

  // Log file:

  if (SysLogLevel > 0)
     openlog("vdr", LOG_CONS, SysLogTarget); // LOG_PID doesn't work as expected under NPTL

  // Check the video directory:

  if (!DirectoryOk(VideoDirectory, true)) {
     fprintf(stderr, "vdr: can't access video directory %s\n", VideoDirectory);
     return 2;
     }

  // Daemon mode:

  if (DaemonMode) {
     if (daemon(1, 0) == -1) {
        fprintf(stderr, "vdr: %m\n");
        esyslog("ERROR: %m");
        return 2;
        }
     }
  else if (Terminal) {
     // Claim new controlling terminal
     stdin  = freopen(Terminal, "r", stdin);
     stdout = freopen(Terminal, "w", stdout);
     stderr = freopen(Terminal, "w", stderr);
     HasStdin = true;
     tcgetattr(STDIN_FILENO, &savedTm);
     }

  isyslog("VDR version %s started", VDRVERSION);
  if (StartedAsRoot && VdrUser)
     isyslog("switched to user '%s'", VdrUser);
  if (DaemonMode)
     dsyslog("running as daemon (tid=%d)", cThread::ThreadId());
  cThread::SetMainThreadId();

  // Set the system character table:

  char *CodeSet = NULL;
  if (setlocale(LC_CTYPE, ""))
     CodeSet = nl_langinfo(CODESET);
  else {
     char *LangEnv = getenv("LANG"); // last resort in case locale stuff isn't installed
     if (LangEnv) {
        CodeSet = strchr(LangEnv, '.');
        if (CodeSet)
           CodeSet++; // skip the dot
        }
     }
  if (CodeSet) {
     bool known = SI::SetSystemCharacterTable(CodeSet);
     isyslog("codeset is '%s' - %s", CodeSet, known ? "known" : "unknown");
     cCharSetConv::SetSystemCharacterTable(CodeSet);
     }

  // Initialize internationalization:

  I18nInitialize(LocaleDir);

  // Main program loop variables - need to be here to have them initialized before any EXIT():

  cEpgDataReader EpgDataReader;
  cOsdObject *Menu = NULL;
  int LastChannel = 0;
  int LastTimerChannel = -1;
  int PreviousChannel[2] = { 1, 1 };
  int PreviousChannelIndex = 0;
  time_t LastChannelChanged = time(NULL);
  time_t LastInteract = 0;
  int MaxLatencyTime = 0;
  bool InhibitEpgScan = false;
  bool IsInfoMenu = false;
  bool CheckHasProgramme = false;
  cSkin *CurrentSkin = NULL;

  // Load plugins:

  if (!PluginManager.LoadPlugins(true))
     EXIT(2);

  // Configuration data:

  if (!ConfigDirectory)
     ConfigDirectory = DEFAULTCONFDIR;

  cPlugin::SetConfigDirectory(ConfigDirectory);
  cThemes::SetThemesDirectory(AddDirectory(ConfigDirectory, "themes"));

  Setup.Load(AddDirectory(ConfigDirectory, "setup.conf"));
  Sources.Load(AddDirectory(ConfigDirectory, "sources.conf"), true, true);
  Diseqcs.Load(AddDirectory(ConfigDirectory, "diseqc.conf"), true, Setup.DiSEqC);
  Scrs.Load(AddDirectory(ConfigDirectory, "scr.conf"), true);
  Channels.Load(AddDirectory(ConfigDirectory, "channels.conf"), false, true);
  Timers.Load(AddDirectory(ConfigDirectory, "timers.conf"));
  Commands.Load(AddDirectory(ConfigDirectory, "commands.conf"));
  RecordingCommands.Load(AddDirectory(ConfigDirectory, "reccmds.conf"));
  SVDRPhosts.Load(AddDirectory(ConfigDirectory, "svdrphosts.conf"), true);
  Keys.Load(AddDirectory(ConfigDirectory, "remote.conf"));
  KeyMacros.Load(AddDirectory(ConfigDirectory, "keymacros.conf"), true);
  Folders.Load(AddDirectory(ConfigDirectory, "folders.conf"));

  if (!*cFont::GetFontFileName(Setup.FontOsd)) {
     const char *msg = "no fonts available - OSD will not show any text!";
     fprintf(stderr, "vdr: %s\n", msg);
     esyslog("ERROR: %s", msg);
     }

  // Recordings:

  Recordings.Update();
  DeletedRecordings.Update();

  // EPG data:

  if (EpgDataFileName) {
     const char *EpgDirectory = NULL;
     if (DirectoryOk(EpgDataFileName)) {
        EpgDirectory = EpgDataFileName;
        EpgDataFileName = DEFAULTEPGDATAFILENAME;
        }
     else if (*EpgDataFileName != '/' && *EpgDataFileName != '.')
        EpgDirectory = VideoDirectory;
     if (EpgDirectory)
        cSchedules::SetEpgDataFileName(AddDirectory(EpgDirectory, EpgDataFileName));
     else
        cSchedules::SetEpgDataFileName(EpgDataFileName);
     EpgDataReader.Start();
     }

  // DVB interfaces:

  cDvbDevice::Initialize();
  cDvbDevice::BondDevices(Setup.DeviceBondings);

  // Initialize plugins:

  if (!PluginManager.InitializePlugins())
     EXIT(2);

  // Primary device:

  cDevice::SetPrimaryDevice(Setup.PrimaryDVB);
  if (!cDevice::PrimaryDevice() || !cDevice::PrimaryDevice()->HasDecoder()) {
     if (cDevice::PrimaryDevice() && !cDevice::PrimaryDevice()->HasDecoder())
        isyslog("device %d has no MPEG decoder", cDevice::PrimaryDevice()->DeviceNumber() + 1);
     for (int i = 0; i < cDevice::NumDevices(); i++) {
         cDevice *d = cDevice::GetDevice(i);
         if (d && d->HasDecoder()) {
            isyslog("trying device number %d instead", i + 1);
            if (cDevice::SetPrimaryDevice(i + 1)) {
               Setup.PrimaryDVB = i + 1;
               break;
               }
            }
         }
     if (!cDevice::PrimaryDevice()) {
        const char *msg = "no primary device found - using first device!";
        fprintf(stderr, "vdr: %s\n", msg);
        esyslog("ERROR: %s", msg);
        if (!cDevice::SetPrimaryDevice(1))
           EXIT(2);
        if (!cDevice::PrimaryDevice()) {
           const char *msg = "no primary device found - giving up!";
           fprintf(stderr, "vdr: %s\n", msg);
           esyslog("ERROR: %s", msg);
           EXIT(2);
           }
        }
     }

  // Check for timers in automatic start time window:

  ShutdownHandler.CheckManualStart(MANUALSTART);

  // User interface:

  Interface = new cInterface(SVDRPport);

  // Default skins:

  new cSkinLCARS;
  new cSkinSTTNG;
  new cSkinClassic;
  Skins.SetCurrent(Setup.OSDSkin);
  cThemes::Load(Skins.Current()->Name(), Setup.OSDTheme, Skins.Current()->Theme());
  CurrentSkin = Skins.Current();

  // Start plugins:

  if (!PluginManager.StartPlugins())
     EXIT(2);

  // Set skin and theme in case they're implemented by a plugin:

  if (!CurrentSkin || CurrentSkin == Skins.Current() && strcmp(Skins.Current()->Name(), Setup.OSDSkin) != 0) {
     Skins.SetCurrent(Setup.OSDSkin);
     cThemes::Load(Skins.Current()->Name(), Setup.OSDTheme, Skins.Current()->Theme());
     }

  // Remote Controls:
  if (LircDevice)
     new cLircRemote(LircDevice);
  if (!DaemonMode && HasStdin && UseKbd)
     new cKbdRemote;
  Interface->LearnKeys();

  // External audio:

  if (AudioCommand)
     new cExternalAudio(AudioCommand);

  // Channel:

  if (!cDevice::WaitForAllDevicesReady(DEVICEREADYTIMEOUT))
     dsyslog("not all devices ready after %d seconds", DEVICEREADYTIMEOUT);
  if (*Setup.InitialChannel) {
     if (isnumber(Setup.InitialChannel)) { // for compatibility with old setup.conf files
        if (cChannel *Channel = Channels.GetByNumber(atoi(Setup.InitialChannel)))
           Setup.InitialChannel = Channel->GetChannelID().ToString();
        }
     if (cChannel *Channel = Channels.GetByChannelID(tChannelID::FromString(Setup.InitialChannel)))
        Setup.CurrentChannel = Channel->Number();
     }
  if (Setup.InitialVolume >= 0)
     Setup.CurrentVolume = Setup.InitialVolume;
  Channels.SwitchTo(Setup.CurrentChannel);
  if (MuteAudio)
     cDevice::PrimaryDevice()->ToggleMute();
  else
     cDevice::PrimaryDevice()->SetVolume(Setup.CurrentVolume, true);

  // Signal handlers:

  if (signal(SIGHUP,  SignalHandler) == SIG_IGN) signal(SIGHUP,  SIG_IGN);
  if (signal(SIGINT,  SignalHandler) == SIG_IGN) signal(SIGINT,  SIG_IGN);
  if (signal(SIGTERM, SignalHandler) == SIG_IGN) signal(SIGTERM, SIG_IGN);
  if (signal(SIGPIPE, SignalHandler) == SIG_IGN) signal(SIGPIPE, SIG_IGN);
  if (WatchdogTimeout > 0)
     if (signal(SIGALRM, Watchdog)   == SIG_IGN) signal(SIGALRM, SIG_IGN);

  // Watchdog:

  if (WatchdogTimeout > 0) {
     dsyslog("setting watchdog timer to %d seconds", WatchdogTimeout);
     alarm(WatchdogTimeout); // Initial watchdog timer start
     }

  // Main program loop:

#define DELETE_MENU ((IsInfoMenu &= (Menu == NULL)), delete Menu, Menu = NULL)

  while (!ShutdownHandler.DoExit()) {
#ifdef DEBUGRINGBUFFERS
        cRingBufferLinear::PrintDebugRBL();
#endif
        // Attach launched player control:
        cControl::Attach();

        time_t Now = time(NULL);

        // Make sure we have a visible programme in case device usage has changed:
        if (!EITScanner.Active() && cDevice::PrimaryDevice()->HasDecoder() && !cDevice::PrimaryDevice()->HasProgramme()) {
           static time_t lastTime = 0;
           if ((!Menu || CheckHasProgramme) && Now - lastTime > MINCHANNELWAIT) { // !Menu to avoid interfering with the CAM if a CAM menu is open
              cChannel *Channel = Channels.GetByNumber(cDevice::CurrentChannel());
              if (Channel && (Channel->Vpid() || Channel->Apid(0) || Channel->Dpid(0))) {
                 if (cDevice::GetDeviceForTransponder(Channel, LIVEPRIORITY) && Channels.SwitchTo(Channel->Number())) // try to switch to the original channel...
                    ;
                 else if (LastTimerChannel > 0) {
                    Channel = Channels.GetByNumber(LastTimerChannel);
                    if (Channel && cDevice::GetDeviceForTransponder(Channel, LIVEPRIORITY) && Channels.SwitchTo(LastTimerChannel)) // ...or the one used by the last timer
                       ;
                    }
                 }
              lastTime = Now; // don't do this too often
              LastTimerChannel = -1;
              CheckHasProgramme = false;
              }
           }
        // Update the OSD size:
        {
          static time_t lastOsdSizeUpdate = 0;
          if (Now != lastOsdSizeUpdate) { // once per second
             cOsdProvider::UpdateOsdSize();
             lastOsdSizeUpdate = Now;
             }
        }
        // Restart the Watchdog timer:
        if (WatchdogTimeout > 0) {
           int LatencyTime = WatchdogTimeout - alarm(WatchdogTimeout);
           if (LatencyTime > MaxLatencyTime) {
              MaxLatencyTime = LatencyTime;
              dsyslog("max. latency time %d seconds", MaxLatencyTime);
              }
           }
        // Handle channel and timer modifications:
        if (!Channels.BeingEdited() && !Timers.BeingEdited()) {
           int modified = Channels.Modified();
           static time_t ChannelSaveTimeout = 0;
           static int TimerState = 0;
           // Channels and timers need to be stored in a consistent manner,
           // therefore if one of them is changed, we save both.
           if (modified == CHANNELSMOD_USER || Timers.Modified(TimerState))
              ChannelSaveTimeout = 1; // triggers an immediate save
           else if (modified && !ChannelSaveTimeout)
              ChannelSaveTimeout = Now + CHANNELSAVEDELTA;
           bool timeout = ChannelSaveTimeout == 1 || ChannelSaveTimeout && Now > ChannelSaveTimeout && !cRecordControls::Active();
           if ((modified || timeout) && Channels.Lock(false, 100)) {
              if (timeout) {
                 Channels.Save();
                 Timers.Save();
                 ChannelSaveTimeout = 0;
                 }
              for (cChannel *Channel = Channels.First(); Channel; Channel = Channels.Next(Channel)) {
                  if (Channel->Modification(CHANNELMOD_RETUNE)) {
                     cRecordControls::ChannelDataModified(Channel);
                     if (Channel->Number() == cDevice::CurrentChannel()) {
                        if (!cDevice::PrimaryDevice()->Replaying() || cDevice::PrimaryDevice()->Transferring()) {
                           if (cDevice::ActualDevice()->ProvidesTransponder(Channel)) { // avoids retune on devices that don't really access the transponder
                              isyslog("retuning due to modification of channel %d", Channel->Number());
                              Channels.SwitchTo(Channel->Number());
                              }
                           }
                        }
                     }
                  }
              Channels.Unlock();
              }
           }
        // Channel display:
        if (!EITScanner.Active() && cDevice::CurrentChannel() != LastChannel) {
           if (!Menu)
              Menu = new cDisplayChannel(cDevice::CurrentChannel(), LastChannel >= 0);
           LastChannel = cDevice::CurrentChannel();
           LastChannelChanged = Now;
           }
        if (Now - LastChannelChanged >= Setup.ZapTimeout && LastChannel != PreviousChannel[PreviousChannelIndex])
           PreviousChannel[PreviousChannelIndex ^= 1] = LastChannel;
        // Timers and Recordings:
        if (!Timers.BeingEdited()) {
           // Assign events to timers:
           Timers.SetEvents();
           // Must do all following calls with the exact same time!
           // Process ongoing recordings:
           cRecordControls::Process(Now);
           // Start new recordings:
           cTimer *Timer = Timers.GetMatch(Now);
           if (Timer) {
              if (!cRecordControls::Start(Timer))
                 Timer->SetPending(true);
              else
                 LastTimerChannel = Timer->Channel()->Number();
              }
           // Make sure timers "see" their channel early enough:
           static time_t LastTimerCheck = 0;
           if (Now - LastTimerCheck > TIMERCHECKDELTA) { // don't do this too often
              InhibitEpgScan = false;
              for (cTimer *Timer = Timers.First(); Timer; Timer = Timers.Next(Timer)) {
                  bool InVpsMargin = false;
                  bool NeedsTransponder = false;
                  if (Timer->HasFlags(tfActive) && !Timer->Recording()) {
                     if (Timer->HasFlags(tfVps)) {
                        if (Timer->Matches(Now, true, Setup.VpsMargin)) {
                           InVpsMargin = true;
                           Timer->SetInVpsMargin(InVpsMargin);
                           }
                        else if (Timer->Event()) {
                           InVpsMargin = Timer->Event()->StartTime() <= Now && Timer->Event()->RunningStatus() == SI::RunningStatusUndefined;
                           NeedsTransponder = Timer->Event()->StartTime() - Now < VPSLOOKAHEADTIME * 3600 && !Timer->Event()->SeenWithin(VPSUPTODATETIME);
                           }
                        else {
                           cSchedulesLock SchedulesLock;
                           const cSchedules *Schedules = cSchedules::Schedules(SchedulesLock);
                           if (Schedules) {
                              const cSchedule *Schedule = Schedules->GetSchedule(Timer->Channel());
                              InVpsMargin = !Schedule; // we must make sure we have the schedule
                              NeedsTransponder = Schedule && !Schedule->PresentSeenWithin(VPSUPTODATETIME);
                              }
                           }
                        InhibitEpgScan |= InVpsMargin | NeedsTransponder;
                        }
                     else
                        NeedsTransponder = Timer->Matches(Now, true, TIMERLOOKAHEADTIME);
                     }
                  if (NeedsTransponder || InVpsMargin) {
                     // Find a device that provides the required transponder:
                     cDevice *Device = cDevice::GetDeviceForTransponder(Timer->Channel(), MINPRIORITY);
                     if (!Device && InVpsMargin)
                        Device = cDevice::GetDeviceForTransponder(Timer->Channel(), LIVEPRIORITY);
                     // Switch the device to the transponder:
                     if (Device) {
                        bool HadProgramme = cDevice::PrimaryDevice()->HasProgramme();
                        if (!Device->IsTunedToTransponder(Timer->Channel())) {
                           if (Device == cDevice::ActualDevice() && !Device->IsPrimaryDevice())
                              cDevice::PrimaryDevice()->StopReplay(); // stop transfer mode
                           dsyslog("switching device %d to channel %d", Device->DeviceNumber() + 1, Timer->Channel()->Number());
                           if (Device->SwitchChannel(Timer->Channel(), false))
                              Device->SetOccupied(TIMERDEVICETIMEOUT);
                           }
                        if (cDevice::PrimaryDevice()->HasDecoder() && HadProgramme && !cDevice::PrimaryDevice()->HasProgramme())
                           Skins.Message(mtInfo, tr("Upcoming recording!")); // the previous SwitchChannel() has switched away the current live channel
                        }
                     }
                  }
              LastTimerCheck = Now;
              }
           // Delete expired timers:
           Timers.DeleteExpired();
           }
        if (!Menu && Recordings.NeedsUpdate()) {
           Recordings.Update();
           DeletedRecordings.Update();
           }
        // CAM control:
        if (!Menu && !cOsd::IsOpen())
           Menu = CamControl();
        // Queued messages:
        if (!Skins.IsOpen())
           Skins.ProcessQueuedMessages();
        // User Input:
        cOsdObject *Interact = Menu ? Menu : cControl::Control();
        eKeys key = Interface->GetKey(!Interact || !Interact->NeedsFastResponse());
        if (ISREALKEY(key)) {
           EITScanner.Activity();
           // Cancel shutdown countdown:
           if (ShutdownHandler.countdown)
              ShutdownHandler.countdown.Cancel();
           // Set user active for MinUserInactivity time in the future:
           ShutdownHandler.SetUserInactiveTimeout();
           }
        // Keys that must work independent of any interactive mode:
        switch (int(key)) {
          // Menu control:
          case kMenu: {
               key = kNone; // nobody else needs to see this key
               bool WasOpen = Interact != NULL;
               bool WasMenu = Interact && Interact->IsMenu();
               if (Menu)
                  DELETE_MENU;
               else if (cControl::Control()) {
                  if (cOsd::IsOpen())
                     cControl::Control()->Hide();
                  else
                     WasOpen = false;
                  }
               if (!WasOpen || !WasMenu && !Setup.MenuKeyCloses)
                  Menu = new cMenuMain;
               }
               break;
          // Info:
          case kInfo: {
               if (IsInfoMenu) {
                  key = kNone; // nobody else needs to see this key
                  DELETE_MENU;
                  }
               else if (!Menu) {
                  IsInfoMenu = true;
                  if (cControl::Control()) {
                     cControl::Control()->Hide();
                     Menu = cControl::Control()->GetInfo();
                     if (Menu)
                        Menu->Show();
                     else
                        IsInfoMenu = false;
                     }
                  else {
                     cRemote::Put(kOk, true);
                     cRemote::Put(kSchedule, true);
                     }
                  key = kNone; // nobody else needs to see this key
                  }
               }
               break;
          // Direct main menu functions:
          #define DirectMainFunction(function)\
            DELETE_MENU;\
            if (cControl::Control())\
               cControl::Control()->Hide();\
            Menu = new cMenuMain(function);\
            key = kNone; // nobody else needs to see this key
          case kSchedule:   DirectMainFunction(osSchedule); break;
          case kChannels:   DirectMainFunction(osChannels); break;
          case kTimers:     DirectMainFunction(osTimers); break;
          case kRecordings: DirectMainFunction(osRecordings); break;
          case kSetup:      DirectMainFunction(osSetup); break;
          case kCommands:   DirectMainFunction(osCommands); break;
          case kUser0 ... kUser9: cRemote::PutMacro(key); key = kNone; break;
          case k_Plugin: {
               const char *PluginName = cRemote::GetPlugin();
               if (PluginName) {
                  DELETE_MENU;
                  if (cControl::Control())
                     cControl::Control()->Hide();
                  cPlugin *plugin = cPluginManager::GetPlugin(PluginName);
                  if (plugin) {
                     Menu = plugin->MainMenuAction();
                     if (Menu)
                        Menu->Show();
                     }
                  else
                     esyslog("ERROR: unknown plugin '%s'", PluginName);
                  }
               key = kNone; // nobody else needs to see these keys
               }
               break;
          // Channel up/down:
          case kChanUp|k_Repeat:
          case kChanUp:
          case kChanDn|k_Repeat:
          case kChanDn:
               if (!Interact)
                  Menu = new cDisplayChannel(NORMALKEY(key));
               else if (cDisplayChannel::IsOpen() || cControl::Control()) {
                  Interact->ProcessKey(key);
                  continue;
                  }
               else
                  cDevice::SwitchChannel(NORMALKEY(key) == kChanUp ? 1 : -1);
               key = kNone; // nobody else needs to see these keys
               break;
          // Volume control:
          case kVolUp|k_Repeat:
          case kVolUp:
          case kVolDn|k_Repeat:
          case kVolDn:
          case kMute:
               if (key == kMute) {
                  if (!cDevice::PrimaryDevice()->ToggleMute() && !Menu) {
                     key = kNone; // nobody else needs to see these keys
                     break; // no need to display "mute off"
                     }
                  }
               else
                  cDevice::PrimaryDevice()->SetVolume(NORMALKEY(key) == kVolDn ? -VOLUMEDELTA : VOLUMEDELTA);
               if (!Menu && !cOsd::IsOpen())
                  Menu = cDisplayVolume::Create();
               cDisplayVolume::Process(key);
               key = kNone; // nobody else needs to see these keys
               break;
          // Audio track control:
          case kAudio:
               if (cControl::Control())
                  cControl::Control()->Hide();
               if (!cDisplayTracks::IsOpen()) {
                  DELETE_MENU;
                  Menu = cDisplayTracks::Create();
                  }
               else
                  cDisplayTracks::Process(key);
               key = kNone;
               break;
          // Subtitle track control:
          case kSubtitles:
               if (cControl::Control())
                  cControl::Control()->Hide();
               if (!cDisplaySubtitleTracks::IsOpen()) {
                  DELETE_MENU;
                  Menu = cDisplaySubtitleTracks::Create();
                  }
               else
                  cDisplaySubtitleTracks::Process(key);
               key = kNone;
               break;
          // Pausing live video:
          case kPause:
               if (!cControl::Control()) {
                  DELETE_MENU;
                  if (Setup.PauseKeyHandling) {
                     if (Setup.PauseKeyHandling > 1 || Interface->Confirm(tr("Pause live video?"))) {
                        if (!cRecordControls::PauseLiveVideo())
                           Skins.Message(mtError, tr("No free DVB device to record!"));
                        }
                     }
                  key = kNone; // nobody else needs to see this key
                  }
               break;
          // Instant recording:
          case kRecord:
               if (!cControl::Control()) {
                  if (cRecordControls::Start())
                     Skins.Message(mtInfo, tr("Recording started"));
                  key = kNone; // nobody else needs to see this key
                  }
               break;
          // Power off:
          case kPower:
               isyslog("Power button pressed");
               DELETE_MENU;
               // Check for activity, request power button again if active:
               if (!ShutdownHandler.ConfirmShutdown(false) && Skins.Message(mtWarning, tr("VDR will shut down later - press Power to force"), SHUTDOWNFORCEPROMPT) != kPower) {
                  // Not pressed power - set VDR to be non-interactive and power down later:
                  ShutdownHandler.SetUserInactive();
                  break;
                  }
               // No activity or power button pressed twice - ask for confirmation:
               if (!ShutdownHandler.ConfirmShutdown(true)) {
                  // Non-confirmed background activity - set VDR to be non-interactive and power down later:
                  ShutdownHandler.SetUserInactive();
                  break;
                  }
               // Ask the final question:
               if (!Interface->Confirm(tr("Press any key to cancel shutdown"), SHUTDOWNCANCELPROMPT, true))
                  // If final question was canceled, continue to be active:
                  break;
               // Ok, now call the shutdown script:
               ShutdownHandler.DoShutdown(true);
               // Set VDR to be non-interactive and power down again later:
               ShutdownHandler.SetUserInactive();
               // Do not attempt to automatically shut down for a while:
               ShutdownHandler.SetRetry(SHUTDOWNRETRY);
               break;
          default: break;
          }
        Interact = Menu ? Menu : cControl::Control(); // might have been closed in the mean time
        if (Interact) {
           LastInteract = Now;
           eOSState state = Interact->ProcessKey(key);
           if (state == osUnknown && Interact != cControl::Control()) {
              if (ISMODELESSKEY(key) && cControl::Control()) {
                 state = cControl::Control()->ProcessKey(key);
                 if (state == osEnd) {
                    // let's not close a menu when replay ends:
                    cControl::Shutdown();
                    continue;
                    }
                 }
              else if (Now - cRemote::LastActivity() > MENUTIMEOUT)
                 state = osEnd;
              }
           switch (state) {
             case osPause:  DELETE_MENU;
                            if (!cRecordControls::PauseLiveVideo())
                               Skins.Message(mtError, tr("No free DVB device to record!"));
                            break;
             case osRecord: DELETE_MENU;
                            if (cRecordControls::Start())
                               Skins.Message(mtInfo, tr("Recording started"));
                            break;
             case osRecordings:
                            DELETE_MENU;
                            cControl::Shutdown();
                            Menu = new cMenuMain(osRecordings);
                            CheckHasProgramme = true; // to have live tv after stopping replay with 'Back'
                            break;
             case osReplay: DELETE_MENU;
                            cControl::Shutdown();
                            cControl::Launch(new cReplayControl);
                            break;
             case osStopReplay:
                            DELETE_MENU;
                            cControl::Shutdown();
                            break;
             case osSwitchDvb:
                            DELETE_MENU;
                            cControl::Shutdown();
                            Skins.Message(mtInfo, tr("Switching primary DVB..."));
                            cDevice::SetPrimaryDevice(Setup.PrimaryDVB);
                            break;
             case osPlugin: DELETE_MENU;
                            Menu = cMenuMain::PluginOsdObject();
                            if (Menu)
                               Menu->Show();
                            break;
             case osBack:
             case osEnd:    if (Interact == Menu)
                               DELETE_MENU;
                            else
                               cControl::Shutdown();
                            break;
             default:       ;
             }
           }
        else {
           // Key functions in "normal" viewing mode:
           if (key != kNone && KeyMacros.Get(key)) {
              cRemote::PutMacro(key);
              key = kNone;
              }
           switch (int(key)) {
             // Toggle channels:
             case kChanPrev:
             case k0: {
                  if (PreviousChannel[PreviousChannelIndex ^ 1] == LastChannel || LastChannel != PreviousChannel[0] && LastChannel != PreviousChannel[1])
                     PreviousChannelIndex ^= 1;
                  Channels.SwitchTo(PreviousChannel[PreviousChannelIndex ^= 1]);
                  break;
                  }
             // Direct Channel Select:
             case k1 ... k9:
             // Left/Right rotates through channel groups:
             case kLeft|k_Repeat:
             case kLeft:
             case kRight|k_Repeat:
             case kRight:
             // Previous/Next rotates through channel groups:
             case kPrev|k_Repeat:
             case kPrev:
             case kNext|k_Repeat:
             case kNext:
             // Up/Down Channel Select:
             case kUp|k_Repeat:
             case kUp:
             case kDown|k_Repeat:
             case kDown:
                  Menu = new cDisplayChannel(NORMALKEY(key));
                  break;
             // Viewing Control:
             case kOk:   LastChannel = -1; break; // forces channel display
             // Instant resume of the last viewed recording:
             case kPlay:
                  if (cReplayControl::LastReplayed()) {
                     cControl::Shutdown();
                     cControl::Launch(new cReplayControl);
                     }
                  break;
             default:    break;
             }
           }
        if (!Menu) {
           if (!InhibitEpgScan)
              EITScanner.Process();
           if (!cCutter::Active() && cCutter::Ended()) {
              if (cCutter::Error())
                 Skins.Message(mtError, tr("Editing process failed!"));
              else
                 Skins.Message(mtInfo, tr("Editing process finished"));
              }
           }

        // SIGHUP shall cause a restart:
        if (LastSignal == SIGHUP) {
           if (ShutdownHandler.ConfirmRestart(true) && Interface->Confirm(tr("Press any key to cancel restart"), RESTARTCANCELPROMPT, true))
              EXIT(1);
           LastSignal = 0;
           }

        // Update the shutdown countdown:
        if (ShutdownHandler.countdown && ShutdownHandler.countdown.Update()) {
           if (!ShutdownHandler.ConfirmShutdown(false))
              ShutdownHandler.countdown.Cancel();
           }

        if ((Now - LastInteract) > ACTIVITYTIMEOUT && !cRecordControls::Active() && !cCutter::Active() && !Interface->HasSVDRPConnection() && (Now - cRemote::LastActivity()) > ACTIVITYTIMEOUT) {
           // Handle housekeeping tasks

           // Shutdown:
           // Check whether VDR will be ready for shutdown in SHUTDOWNWAIT seconds:
           time_t Soon = Now + SHUTDOWNWAIT;
           if (ShutdownHandler.IsUserInactive(Soon) && ShutdownHandler.Retry(Soon) && !ShutdownHandler.countdown) {
              if (ShutdownHandler.ConfirmShutdown(false))
                 // Time to shut down - start final countdown:
                 ShutdownHandler.countdown.Start(tr("VDR will shut down in %s minutes"), SHUTDOWNWAIT); // the placeholder is really %s!
              // Dont try to shut down again for a while:
              ShutdownHandler.SetRetry(SHUTDOWNRETRY);
              }
           // Countdown run down to 0?
           if (ShutdownHandler.countdown.Done()) {
              // Timed out, now do a final check:
              if (ShutdownHandler.IsUserInactive() && ShutdownHandler.ConfirmShutdown(false))
                 ShutdownHandler.DoShutdown(false);
              // Do this again a bit later:
              ShutdownHandler.SetRetry(SHUTDOWNRETRY);
              }

           // Disk housekeeping:
           RemoveDeletedRecordings();
           cSchedules::Cleanup();
           // Plugins housekeeping:
           PluginManager.Housekeeping();
           }

        // Main thread hooks of plugins:
        PluginManager.MainThreadHook();
        }

  if (ShutdownHandler.EmergencyExitRequested())
     esyslog("emergency exit requested - shutting down");

Exit:

  // Reset all signal handlers to default before Interface gets deleted:
  signal(SIGHUP,  SIG_DFL);
  signal(SIGINT,  SIG_DFL);
  signal(SIGTERM, SIG_DFL);
  signal(SIGPIPE, SIG_DFL);
  signal(SIGALRM, SIG_DFL);

  PluginManager.StopPlugins();
  cRecordControls::Shutdown();
  cCutter::Stop();
  delete Menu;
  cControl::Shutdown();
  delete Interface;
  cOsdProvider::Shutdown();
  Remotes.Clear();
  Audios.Clear();
  Skins.Clear();
  SourceParams.Clear();
  if (ShutdownHandler.GetExitCode() != 2) {
     Setup.CurrentChannel = cDevice::CurrentChannel();
     Setup.CurrentVolume  = cDevice::CurrentVolume();
     Setup.Save();
     }
  cDevice::Shutdown();
  EpgHandlers.Clear();
  PluginManager.Shutdown(true);
  cSchedules::Cleanup(true);
  ReportEpgBugFixStats();
  if (WatchdogTimeout > 0)
     dsyslog("max. latency time %d seconds", MaxLatencyTime);
  if (LastSignal)
     isyslog("caught signal %d", LastSignal);
  if (ShutdownHandler.EmergencyExitRequested())
     esyslog("emergency exit!");
  isyslog("exiting, exit code %d", ShutdownHandler.GetExitCode());
  if (SysLogLevel > 0)
     closelog();
  if (HasStdin)
     tcsetattr(STDIN_FILENO, TCSANOW, &savedTm);
  return ShutdownHandler.GetExitCode();
}
