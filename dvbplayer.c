/*
 * dvbplayer.c: The DVB player
 *
 * See the main source file 'vdr.c' for copyright information and
 * how to reach the author.
 *
 * $Id: dvbplayer.c 2.27 2012/05/06 11:02:35 kls Exp $
 */

#include "dvbplayer.h"
#include <math.h>
#include <stdlib.h>
#include "recording.h"
#include "remux.h"
#include "ringbuffer.h"
#include "thread.h"
#include "tools.h"

// --- cPtsIndex -------------------------------------------------------------

#define PTSINDEX_ENTRIES 500

class cPtsIndex {
private:
  struct tPtsIndex {
    uint32_t pts; // no need for 33 bit - some devices don't even supply the msb
    int index;
    };
  tPtsIndex pi[PTSINDEX_ENTRIES];
  int w, r;
  int lastFound;
  cMutex mutex;
public:
  cPtsIndex(void);
  void Clear(void);
  void Put(uint32_t Pts, int Index);
  int FindIndex(uint32_t Pts);
  };

cPtsIndex::cPtsIndex(void)
{
  lastFound = 0;
  Clear();
}

void cPtsIndex::Clear(void)
{
  cMutexLock MutexLock(&mutex);
  w = r = 0;
}

void cPtsIndex::Put(uint32_t Pts, int Index)
{
  cMutexLock MutexLock(&mutex);
  pi[w].pts = Pts;
  pi[w].index = Index;
  w = (w + 1) % PTSINDEX_ENTRIES;
  if (w == r)
     r = (r + 1) % PTSINDEX_ENTRIES;
}

int cPtsIndex::FindIndex(uint32_t Pts)
{
  cMutexLock MutexLock(&mutex);
  if (w == r)
     return lastFound; // list is empty, let's not jump way off the last known position
  uint32_t Delta = 0xFFFFFFFF;
  int Index = -1;
  for (int i = w; i != r; ) {
      if (--i < 0)
         i = PTSINDEX_ENTRIES - 1;
      uint32_t d = pi[i].pts < Pts ? Pts - pi[i].pts : pi[i].pts - Pts;
      if (d > 0x7FFFFFFF)
         d = 0xFFFFFFFF - d; // handle rollover
      if (d < Delta) {
         Delta = d;
         Index = pi[i].index;
         }
      }
  lastFound = Index;
  return Index;
}

// --- cNonBlockingFileReader ------------------------------------------------

class cNonBlockingFileReader : public cThread {
private:
  cUnbufferedFile *f;
  uchar *buffer;
  int wanted;
  int length;
  cCondWait newSet;
  cCondVar newDataCond;
  cMutex newDataMutex;
protected:
  void Action(void);
public:
  cNonBlockingFileReader(void);
  ~cNonBlockingFileReader();
  void Clear(void);
  void Request(cUnbufferedFile *File, int Length);
  int Result(uchar **Buffer);
  bool Reading(void) { return buffer; }
  bool WaitForDataMs(int msToWait);
  };

cNonBlockingFileReader::cNonBlockingFileReader(void)
:cThread("non blocking file reader")
{
  f = NULL;
  buffer = NULL;
  wanted = length = 0;
  Start();
}

cNonBlockingFileReader::~cNonBlockingFileReader()
{
  newSet.Signal();
  Cancel(3);
  free(buffer);
}

void cNonBlockingFileReader::Clear(void)
{
  Lock();
  f = NULL;
  free(buffer);
  buffer = NULL;
  wanted = length = 0;
  Unlock();
}

void cNonBlockingFileReader::Request(cUnbufferedFile *File, int Length)
{
  Lock();
  Clear();
  wanted = Length;
  buffer = MALLOC(uchar, wanted);
  f = File;
  Unlock();
  newSet.Signal();
}

int cNonBlockingFileReader::Result(uchar **Buffer)
{
  LOCK_THREAD;
  if (buffer && length == wanted) {
     *Buffer = buffer;
     buffer = NULL;
     return wanted;
     }
  errno = EAGAIN;
  return -1;
}

void cNonBlockingFileReader::Action(void)
{
  while (Running()) {
        Lock();
        if (f && buffer && length < wanted) {
           int r = f->Read(buffer + length, wanted - length);
           if (r > 0)
              length += r;
           else if (r == 0) { // r == 0 means EOF
              if (length > 0)
                 wanted = length; // already read something, so return the rest
              else
                 length = wanted = 0; // report EOF
              }
           else if (FATALERRNO) {
              LOG_ERROR;
              length = wanted = r; // this will forward the error status to the caller
              }
           if (length == wanted) {
              cMutexLock NewDataLock(&newDataMutex);
              newDataCond.Broadcast();
              }
           }
        Unlock();
        newSet.Wait(1000);
        }
}

bool cNonBlockingFileReader::WaitForDataMs(int msToWait)
{
  cMutexLock NewDataLock(&newDataMutex);
  if (buffer && length == wanted)
     return true;
  return newDataCond.TimedWait(newDataMutex, msToWait);
}

// --- cDvbPlayer ------------------------------------------------------------

#define PLAYERBUFSIZE  MEGABYTE(1)

#define RESUMEBACKUP 10 // number of seconds to back up when resuming an interrupted replay session
#define MAXSTUCKATEOF 3 // max. number of seconds to wait in case the device doesn't play the last frame

class cDvbPlayer : public cPlayer, cThread {
private:
  enum ePlayModes { pmPlay, pmPause, pmSlow, pmFast, pmStill };
  enum ePlayDirs { pdForward, pdBackward };
  static int Speeds[];
  cNonBlockingFileReader *nonBlockingFileReader;
  cRingBufferFrame *ringBuffer;
  cPtsIndex ptsIndex;
  cFileName *fileName;
  cIndexFile *index;
  cUnbufferedFile *replayFile;
  double framesPerSecond;
  bool isPesRecording;
  bool pauseLive;
  bool eof;
  bool firstPacket;
  ePlayModes playMode;
  ePlayDirs playDir;
  int trickSpeed;
  int readIndex;
  bool readIndependent;
  cFrame *readFrame;
  cFrame *playFrame;
  cFrame *dropFrame;
  void TrickSpeed(int Increment);
  void Empty(void);
  bool NextFile(uint16_t FileNumber = 0, off_t FileOffset = -1);
  int Resume(void);
  bool Save(void);
protected:
  virtual void Activate(bool On);
  virtual void Action(void);
public:
  cDvbPlayer(const char *FileName, bool PauseLive);
  virtual ~cDvbPlayer();
  bool Active(void) { return cThread::Running(); }
  void Pause(void);
  void Play(void);
  void Forward(void);
  void Backward(void);
  int SkipFrames(int Frames);
  void SkipSeconds(int Seconds);
  void Goto(int Position, bool Still = false);
  virtual double FramesPerSecond(void) { return framesPerSecond; }
  virtual bool GetIndex(int &Current, int &Total, bool SnapToIFrame = false);
  virtual bool GetReplayMode(bool &Play, bool &Forward, int &Speed);
  };

#define MAX_VIDEO_SLOWMOTION 63 // max. arg to pass to VIDEO_SLOWMOTION // TODO is this value correct?
#define NORMAL_SPEED  4 // the index of the '1' entry in the following array
#define MAX_SPEEDS    3 // the offset of the maximum speed from normal speed in either direction
#define SPEED_MULT   12 // the speed multiplier
int cDvbPlayer::Speeds[] = { 0, -2, -4, -8, 1, 2, 4, 12, 0 };

cDvbPlayer::cDvbPlayer(const char *FileName, bool PauseLive)
:cThread("dvbplayer")
{
  nonBlockingFileReader = NULL;
  ringBuffer = NULL;
  index = NULL;
  cRecording Recording(FileName);
  framesPerSecond = Recording.FramesPerSecond();
  isPesRecording = Recording.IsPesRecording();
  pauseLive = PauseLive;
  eof = false;
  firstPacket = true;
  playMode = pmPlay;
  playDir = pdForward;
  trickSpeed = NORMAL_SPEED;
  readIndex = -1;
  readIndependent = false;
  readFrame = NULL;
  playFrame = NULL;
  dropFrame = NULL;
  isyslog("replay %s", FileName);
  fileName = new cFileName(FileName, false, false, isPesRecording);
  replayFile = fileName->Open();
  if (!replayFile)
     return;
  ringBuffer = new cRingBufferFrame(PLAYERBUFSIZE);
  // Create the index file:
  index = new cIndexFile(FileName, false, isPesRecording, pauseLive);
  if (!index)
     esyslog("ERROR: can't allocate index");
  else if (!index->Ok()) {
     delete index;
     index = NULL;
     }
  else if (PauseLive)
     framesPerSecond = cRecording(FileName).FramesPerSecond(); // the fps rate might have changed from the default
}

cDvbPlayer::~cDvbPlayer()
{
  Save();
  Detach();
  delete readFrame; // might not have been stored in the buffer in Action()
  delete index;
  delete fileName;
  delete ringBuffer;
}

void cDvbPlayer::TrickSpeed(int Increment)
{
  int nts = trickSpeed + Increment;
  if (Speeds[nts] == 1) {
     trickSpeed = nts;
     if (playMode == pmFast)
        Play();
     else
        Pause();
     }
  else if (Speeds[nts]) {
     trickSpeed = nts;
     int Mult = (playMode == pmSlow && playDir == pdForward) ? 1 : SPEED_MULT;
     int sp = (Speeds[nts] > 0) ? Mult / Speeds[nts] : -Speeds[nts] * Mult;
     if (sp > MAX_VIDEO_SLOWMOTION)
        sp = MAX_VIDEO_SLOWMOTION;
     DeviceTrickSpeed(sp);
     }
}

void cDvbPlayer::Empty(void)
{
  LOCK_THREAD;
  if (nonBlockingFileReader)
     nonBlockingFileReader->Clear();
  if (!firstPacket) // don't set the readIndex twice if Empty() is called more than once
     readIndex = ptsIndex.FindIndex(DeviceGetSTC()) - 1;  // Action() will first increment it!
  delete readFrame; // might not have been stored in the buffer in Action()
  readFrame = NULL;
  playFrame = NULL;
  dropFrame = NULL;
  ringBuffer->Clear();
  ptsIndex.Clear();
  DeviceClear();
  firstPacket = true;
}

bool cDvbPlayer::NextFile(uint16_t FileNumber, off_t FileOffset)
{
  if (FileNumber > 0)
     replayFile = fileName->SetOffset(FileNumber, FileOffset);
  else if (replayFile && eof)
     replayFile = fileName->NextFile();
  eof = false;
  return replayFile != NULL;
}

int cDvbPlayer::Resume(void)
{
  if (index) {
     int Index = index->GetResume();
     if (Index >= 0) {
        uint16_t FileNumber;
        off_t FileOffset;
        if (index->Get(Index, &FileNumber, &FileOffset) && NextFile(FileNumber, FileOffset))
           return Index;
        }
     }
  return -1;
}

bool cDvbPlayer::Save(void)
{
  if (index) {
     int Index = ptsIndex.FindIndex(DeviceGetSTC());
     if (Index >= 0) {
        Index -= int(round(RESUMEBACKUP * framesPerSecond));
        if (Index > 0)
           Index = index->GetNextIFrame(Index, false);
        else
           Index = 0;
        if (Index >= 0)
           return index->StoreResume(Index);
        }
     }
  return false;
}

void cDvbPlayer::Activate(bool On)
{
  if (On) {
     if (replayFile)
        Start();
     }
  else
     Cancel(9);
}

void cDvbPlayer::Action(void)
{
  uchar *p = NULL;
  int pc = 0;

  readIndex = Resume();
  if (readIndex >= 0)
     isyslog("resuming replay at index %d (%s)", readIndex, *IndexToHMSF(readIndex, true, framesPerSecond));

  nonBlockingFileReader = new cNonBlockingFileReader;
  int Length = 0;
  bool Sleep = false;
  bool WaitingForData = false;
  time_t StuckAtEof = 0;
  uint32_t LastStc = 0;
  int LastReadIFrame = -1;
  int SwitchToPlayFrame = 0;

  if (pauseLive)
     Goto(0, true);
  while (Running()) {
        if (WaitingForData)
           WaitingForData = !nonBlockingFileReader->WaitForDataMs(3); // this keeps the CPU load low, but reacts immediately on new data
        else if (Sleep) {
           cPoller Poller;
           DevicePoll(Poller, 10);
           Sleep = false;
           if (playMode == pmStill || playMode == pmPause)
              cCondWait::SleepMs(3);
           }
        {
          LOCK_THREAD;

          // Read the next frame from the file:

          if (playMode != pmStill && playMode != pmPause) {
             if (!readFrame && (replayFile || readIndex >= 0)) {
                if (!nonBlockingFileReader->Reading()) {
                   if (!SwitchToPlayFrame && (playMode == pmFast || (playMode == pmSlow && playDir == pdBackward))) {
                      uint16_t FileNumber;
                      off_t FileOffset;
                      bool TimeShiftMode = index->IsStillRecording();
                      int Index = -1;
                      readIndependent = false;
                      if (DeviceHasIBPTrickSpeed() && playDir == pdForward) {
                         if (index->Get(readIndex + 1, &FileNumber, &FileOffset, &readIndependent, &Length))
                            Index = readIndex + 1;
                         }
                      else {
                         int d = int(round(0.4 * framesPerSecond));
                         if (playDir != pdForward)
                            d = -d;
                         int NewIndex = readIndex + d;
                         if (NewIndex <= 0 && readIndex > 0)
                            NewIndex = 1; // make sure the very first frame is delivered
                         NewIndex = index->GetNextIFrame(NewIndex, playDir == pdForward, &FileNumber, &FileOffset, &Length);
                         if (NewIndex < 0 && TimeShiftMode && playDir == pdForward)
                            SwitchToPlayFrame = readIndex;
                         Index = NewIndex;
                         readIndependent = true;
                         }
                      if (Index >= 0) {
                         readIndex = Index;
                         if (!NextFile(FileNumber, FileOffset))
                            continue;
                         }
                      else if (!(TimeShiftMode && playDir == pdForward))
                         eof = true;
                      }
                   else if (index) {
                      uint16_t FileNumber;
                      off_t FileOffset;
                      if (index->Get(readIndex + 1, &FileNumber, &FileOffset, &readIndependent, &Length) && NextFile(FileNumber, FileOffset))
                         readIndex++;
                      else
                         eof = true;
                      }
                   else // allows replay even if the index file is missing
                      Length = MAXFRAMESIZE;
                   if (Length == -1)
                      Length = MAXFRAMESIZE; // this means we read up to EOF (see cIndex)
                   else if (Length > MAXFRAMESIZE) {
                      esyslog("ERROR: frame larger than buffer (%d > %d)", Length, MAXFRAMESIZE);
                      Length = MAXFRAMESIZE;
                      }
                   if (!eof)
                      nonBlockingFileReader->Request(replayFile, Length);
                   }
                if (!eof) {
                   uchar *b = NULL;
                   int r = nonBlockingFileReader->Result(&b);
                   if (r > 0) {
                      WaitingForData = false;
                      uint32_t Pts = 0;
                      if (readIndependent) {
                         Pts = isPesRecording ? PesGetPts(b) : TsGetPts(b, r);
                         LastReadIFrame = readIndex;
                         }
                      readFrame = new cFrame(b, -r, ftUnknown, readIndex, Pts); // hands over b to the ringBuffer
                      }
                   else if (r < 0) {
                      if (errno == EAGAIN)
                         WaitingForData = true;
                      else if (FATALERRNO) {
                         LOG_ERROR;
                         break;
                         }
                      }
                   else
                      eof = true;
                   }
                }

             // Store the frame in the buffer:

             if (readFrame) {
                if (ringBuffer->Put(readFrame))
                   readFrame = NULL;
                else
                   Sleep = true;
                }
             }
          else
             Sleep = true;

          if (dropFrame) {
             if (!eof || (playDir != pdForward && dropFrame->Index() > 0) || (playDir == pdForward && dropFrame->Index() < readIndex)) {
                ringBuffer->Drop(dropFrame); // the very first and last frame are continously repeated to flush data through the device
                dropFrame = NULL;
                }
             }

          // Get the next frame from the buffer:

          if (!playFrame) {
             playFrame = ringBuffer->Get();
             p = NULL;
             pc = 0;
             }

          // Play the frame:

          if (playFrame) {
             if (!p) {
                p = playFrame->Data();
                pc = playFrame->Count();
                if (p) {
                   if (playFrame->Index() >= 0 && playFrame->Pts() != 0)
                      ptsIndex.Put(playFrame->Pts(), playFrame->Index());
                   if (firstPacket) {
                      if (isPesRecording) {
                         PlayPes(NULL, 0);
                         cRemux::SetBrokenLink(p, pc);
                         }
                      else
                         PlayTs(NULL, 0);
                      firstPacket = false;
                      }
                   }
                }
             if (p) {
                int w;
                if (isPesRecording)
                   w = PlayPes(p, pc, playMode != pmPlay && !(playMode == pmSlow && playDir == pdForward) && DeviceIsPlayingVideo());
                else
                   w = PlayTs(p, pc, playMode != pmPlay && !(playMode == pmSlow && playDir == pdForward) && DeviceIsPlayingVideo());
                if (w > 0) {
                   p += w;
                   pc -= w;
                   }
                else if (w < 0 && FATALERRNO)
                   LOG_ERROR;
                else
                   Sleep = true;
                }
             if (pc <= 0) {
                dropFrame = playFrame;
                playFrame = NULL;
                p = NULL;
                }
             }
          else
             Sleep = true;

          // Handle hitting begin/end of recording:

          if (eof || SwitchToPlayFrame) {
             bool SwitchToPlay = false;
             uint32_t Stc = DeviceGetSTC();
             if (Stc != LastStc)
                StuckAtEof = 0;
             else if (!StuckAtEof)
                StuckAtEof = time(NULL);
             else if (time(NULL) - StuckAtEof > MAXSTUCKATEOF) {
                if (playDir == pdForward)
                   break; // automatically stop at end of recording
                SwitchToPlay = true;
                }
             LastStc = Stc;
             int Index = ptsIndex.FindIndex(Stc);
             if (playDir == pdForward && !SwitchToPlayFrame) {
                if (Index >= LastReadIFrame)
                   break; // automatically stop at end of recording
                }
             else if (Index <= 0 || SwitchToPlayFrame && Index >= SwitchToPlayFrame)
                SwitchToPlay = true;
             if (SwitchToPlay) {
                if (!SwitchToPlayFrame)
                   Empty();
                DevicePlay();
                playMode = pmPlay;
                playDir = pdForward;
                SwitchToPlayFrame = 0;
                }
             }
        }
        }

  cNonBlockingFileReader *nbfr = nonBlockingFileReader;
  nonBlockingFileReader = NULL;
  delete nbfr;
}

void cDvbPlayer::Pause(void)
{
  if (playMode == pmPause || playMode == pmStill)
     Play();
  else {
     LOCK_THREAD;
     if (playMode == pmFast || (playMode == pmSlow && playDir == pdBackward)) {
        if (!(DeviceHasIBPTrickSpeed() && playDir == pdForward))
           Empty();
        }
     DeviceFreeze();
     playMode = pmPause;
     }
}

void cDvbPlayer::Play(void)
{
  if (playMode != pmPlay) {
     LOCK_THREAD;
     if (playMode == pmStill || playMode == pmFast || (playMode == pmSlow && playDir == pdBackward)) {
        if (!(DeviceHasIBPTrickSpeed() && playDir == pdForward))
           Empty();
        }
     DevicePlay();
     playMode = pmPlay;
     playDir = pdForward;
    }
}

void cDvbPlayer::Forward(void)
{
  if (index) {
     switch (playMode) {
       case pmFast:
            if (Setup.MultiSpeedMode) {
               TrickSpeed(playDir == pdForward ? 1 : -1);
               break;
               }
            else if (playDir == pdForward) {
               Play();
               break;
               }
            // run into pmPlay
       case pmPlay: {
            LOCK_THREAD;
            if (!(DeviceHasIBPTrickSpeed() && playDir == pdForward))
               Empty();
            if (DeviceIsPlayingVideo())
               DeviceMute();
            playMode = pmFast;
            playDir = pdForward;
            trickSpeed = NORMAL_SPEED;
            TrickSpeed(Setup.MultiSpeedMode ? 1 : MAX_SPEEDS);
            }
            break;
       case pmSlow:
            if (Setup.MultiSpeedMode) {
               TrickSpeed(playDir == pdForward ? -1 : 1);
               break;
               }
            else if (playDir == pdForward) {
               Pause();
               break;
               }
            Empty();
            // run into pmPause
       case pmStill:
       case pmPause:
            DeviceMute();
            playMode = pmSlow;
            playDir = pdForward;
            trickSpeed = NORMAL_SPEED;
            TrickSpeed(Setup.MultiSpeedMode ? -1 : -MAX_SPEEDS);
            break;
       default: esyslog("ERROR: unknown playMode %d (%s)", playMode, __FUNCTION__);
       }
     }
}

void cDvbPlayer::Backward(void)
{
  if (index) {
     switch (playMode) {
       case pmFast:
            if (Setup.MultiSpeedMode) {
               TrickSpeed(playDir == pdBackward ? 1 : -1);
               break;
               }
            else if (playDir == pdBackward) {
               Play();
               break;
               }
            // run into pmPlay
       case pmPlay: {
            LOCK_THREAD;
            Empty();
            if (DeviceIsPlayingVideo())
               DeviceMute();
            playMode = pmFast;
            playDir = pdBackward;
            trickSpeed = NORMAL_SPEED;
            TrickSpeed(Setup.MultiSpeedMode ? 1 : MAX_SPEEDS);
            }
            break;
       case pmSlow:
            if (Setup.MultiSpeedMode) {
               TrickSpeed(playDir == pdBackward ? -1 : 1);
               break;
               }
            else if (playDir == pdBackward) {
               Pause();
               break;
               }
            Empty();
            // run into pmPause
       case pmStill:
       case pmPause: {
            LOCK_THREAD;
            Empty();
            DeviceMute();
            playMode = pmSlow;
            playDir = pdBackward;
            trickSpeed = NORMAL_SPEED;
            TrickSpeed(Setup.MultiSpeedMode ? -1 : -MAX_SPEEDS);
            }
            break;
       default: esyslog("ERROR: unknown playMode %d (%s)", playMode, __FUNCTION__);
       }
     }
}

int cDvbPlayer::SkipFrames(int Frames)
{
  if (index && Frames) {
     int Current, Total;
     GetIndex(Current, Total, true);
     int OldCurrent = Current;
     // As GetNextIFrame() increments/decrements at least once, the
     // destination frame (= Current + Frames) must be adjusted by
     // -1/+1 respectively.
     Current = index->GetNextIFrame(Current + Frames + (Frames > 0 ? -1 : 1), Frames > 0);
     return Current >= 0 ? Current : OldCurrent;
     }
  return -1;
}

void cDvbPlayer::SkipSeconds(int Seconds)
{
  if (index && Seconds) {
     LOCK_THREAD;
     int Index = ptsIndex.FindIndex(DeviceGetSTC());
     Empty();
     if (Index >= 0) {
        Index = max(Index + SecondsToFrames(Seconds, framesPerSecond), 0);
        if (Index > 0)
           Index = index->GetNextIFrame(Index, false, NULL, NULL, NULL);
        if (Index >= 0)
           readIndex = Index - 1; // Action() will first increment it!
        }
     Play();
     }
}

void cDvbPlayer::Goto(int Index, bool Still)
{
  if (index) {
     LOCK_THREAD;
     Empty();
     if (++Index <= 0)
        Index = 1; // not '0', to allow GetNextIFrame() below to work!
     uint16_t FileNumber;
     off_t FileOffset;
     int Length;
     Index = index->GetNextIFrame(Index, false, &FileNumber, &FileOffset, &Length);
     if (Index >= 0 && NextFile(FileNumber, FileOffset) && Still) {
        uchar b[MAXFRAMESIZE];
        int r = ReadFrame(replayFile, b, Length, sizeof(b));
        if (r > 0) {
           if (playMode == pmPause)
              DevicePlay();
           DeviceStillPicture(b, r);
           ptsIndex.Put(isPesRecording ? PesGetPts(b) : TsGetPts(b, r), Index);
           }
        playMode = pmStill;
        }
     readIndex = Index;
     }
}

bool cDvbPlayer::GetIndex(int &Current, int &Total, bool SnapToIFrame)
{
  if (index) {
     Current = ptsIndex.FindIndex(DeviceGetSTC());
     if (SnapToIFrame) {
        int i1 = index->GetNextIFrame(Current + 1, false);
        int i2 = index->GetNextIFrame(Current, true);
        Current = (abs(Current - i1) <= abs(Current - i2)) ? i1 : i2;
        }
     Total = index->Last();
     return true;
     }
  Current = Total = -1;
  return false;
}

bool cDvbPlayer::GetReplayMode(bool &Play, bool &Forward, int &Speed)
{
  Play = (playMode == pmPlay || playMode == pmFast);
  Forward = (playDir == pdForward);
  if (playMode == pmFast || playMode == pmSlow)
     Speed = Setup.MultiSpeedMode ? abs(trickSpeed - NORMAL_SPEED) : 0;
  else
     Speed = -1;
  return true;
}

// --- cDvbPlayerControl -----------------------------------------------------

cDvbPlayerControl::cDvbPlayerControl(const char *FileName, bool PauseLive)
:cControl(player = new cDvbPlayer(FileName, PauseLive))
{
}

cDvbPlayerControl::~cDvbPlayerControl()
{
  Stop();
}

bool cDvbPlayerControl::Active(void)
{
  return player && player->Active();
}

void cDvbPlayerControl::Stop(void)
{
  delete player;
  player = NULL;
}

void cDvbPlayerControl::Pause(void)
{
  if (player)
     player->Pause();
}

void cDvbPlayerControl::Play(void)
{
  if (player)
     player->Play();
}

void cDvbPlayerControl::Forward(void)
{
  if (player)
     player->Forward();
}

void cDvbPlayerControl::Backward(void)
{
  if (player)
     player->Backward();
}

void cDvbPlayerControl::SkipSeconds(int Seconds)
{
  if (player)
     player->SkipSeconds(Seconds);
}

int cDvbPlayerControl::SkipFrames(int Frames)
{
  if (player)
     return player->SkipFrames(Frames);
  return -1;
}

bool cDvbPlayerControl::GetIndex(int &Current, int &Total, bool SnapToIFrame)
{
  if (player) {
     player->GetIndex(Current, Total, SnapToIFrame);
     return true;
     }
  return false;
}

bool cDvbPlayerControl::GetReplayMode(bool &Play, bool &Forward, int &Speed)
{
  return player && player->GetReplayMode(Play, Forward, Speed);
}

void cDvbPlayerControl::Goto(int Position, bool Still)
{
  if (player)
     player->Goto(Position, Still);
}
