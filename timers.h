/*
 * timers.h: Timer handling
 *
 * See the main source file 'vdr.c' for copyright information and
 * how to reach the author.
 *
 * $Id: timers.h 3.0 2013/03/11 10:35:53 kls Exp $
 */

#ifndef __TIMERS_H
#define __TIMERS_H

#include "channels.h"
#include "config.h"
#include "epg.h"
#include "tools.h"

enum eTimerFlags { tfNone      = 0x0000,
                   tfActive    = 0x0001,
                   tfInstant   = 0x0002,
                   tfVps       = 0x0004,
                   tfRecording = 0x0008,
                   tfAll       = 0xFFFF,
                 };
enum eTimerMatch { tmNone, tmPartial, tmFull };

class cTimer : public cListObject {
  friend class cMenuEditTimer;
private:
  mutable time_t startTime, stopTime;
  time_t lastSetEvent;
  mutable time_t deferred; ///< Matches(time_t, ...) will return false if the current time is before this value
  bool recording, pending, inVpsMargin;
  uint flags;
  cChannel *channel;
  mutable time_t day; ///< midnight of the day this timer shall hit, or of the first day it shall hit in case of a repeating timer
  int weekdays;       ///< bitmask, lowest bits: SSFTWTM  (the 'M' is the LSB)
  int start;
  int stop;
  int priority;
  int lifetime;
  mutable char file[NAME_MAX * 2 + 1]; // *2 to be able to hold 'title' and 'episode', which can each be up to 255 characters long
  char *aux;
  const cEvent *event;
public:
  cTimer(bool Instant = false, bool Pause = false, cChannel *Channel = NULL);
  cTimer(const cEvent *Event);
  cTimer(const cTimer &Timer);
  virtual ~cTimer();
  cTimer& operator= (const cTimer &Timer);
  virtual int Compare(const cListObject &ListObject) const;
  bool Recording(void) const { return recording; }
  bool Pending(void) const { return pending; }
  bool InVpsMargin(void) const { return inVpsMargin; }
  uint Flags(void) const { return flags; }
  const cChannel *Channel(void) const { return channel; }
  time_t Day(void) const { return day; }
  int WeekDays(void) const { return weekdays; }
  int Start(void) const { return start; }
  int Stop(void) const { return stop; }
  int Priority(void) const { return priority; }
  int Lifetime(void) const { return lifetime; }
  const char *File(void) const { return file; }
  time_t FirstDay(void) const { return weekdays ? day : 0; }
  const char *Aux(void) const { return aux; }
  time_t Deferred(void) const { return deferred; }
  cString ToText(bool UseChannelID = false) const;
  cString ToDescr(void) const;
  const cEvent *Event(void) const { return event; }
  bool Parse(const char *s);
  bool Save(FILE *f);
  bool IsSingleEvent(void) const;
  static int GetMDay(time_t t);
  static int GetWDay(time_t t);
  bool DayMatches(time_t t) const;
  static time_t IncDay(time_t t, int Days);
  static time_t SetTime(time_t t, int SecondsFromMidnight);
  void SetFile(const char *File);
  bool Matches(time_t t = 0, bool Directly = false, int Margin = 0) const;
  eTimerMatch Matches(const cEvent *Event, int *Overlap = NULL) const;
  bool Expired(void) const;
  time_t StartTime(void) const;
  time_t StopTime(void) const;
  void SetEventFromSchedule(const cSchedules *Schedules = NULL);
  void SetEvent(const cEvent *Event);
  void SetRecording(bool Recording);
  void SetPending(bool Pending);
  void SetInVpsMargin(bool InVpsMargin);
  void SetDay(time_t Day);
  void SetWeekDays(int WeekDays);
  void SetStart(int Start);
  void SetStop(int Stop);
  void SetPriority(int Priority);
  void SetLifetime(int Lifetime);
  void SetAux(const char *Aux);
  void SetDeferred(int Seconds);
  void SetFlags(uint Flags);
  void ClrFlags(uint Flags);
  void InvFlags(uint Flags);
  bool HasFlags(uint Flags) const;
  void Skip(void);
  void OnOff(void);
  cString PrintFirstDay(void) const;
  static int TimeToInt(int t);
  static bool ParseDay(const char *s, time_t &Day, int &WeekDays);
  static cString PrintDay(time_t Day, int WeekDays, bool SingleByteChars);
  };

class cTimers : public cConfig<cTimer> {
private:
  int state;
  int beingEdited;
  time_t lastSetEvents;
  time_t lastDeleteExpired;
public:
  cTimers(void);
  cTimer *GetTimer(cTimer *Timer);
  cTimer *GetMatch(time_t t);
  cTimer *GetMatch(const cEvent *Event, eTimerMatch *Match = NULL);
  cTimer *GetNextActiveTimer(void);
  int BeingEdited(void) { return beingEdited; }
  void IncBeingEdited(void) { beingEdited++; }
  void DecBeingEdited(void) { if (!--beingEdited) lastSetEvents = 0; }
  void SetModified(void);
  bool Modified(int &State);
      ///< Returns true if any of the timers have been modified, which
      ///< is detected by State being different than the internal state.
      ///< Upon return the internal state will be stored in State.
  void SetEvents(void);
  void DeleteExpired(void);
  void Add(cTimer *Timer, cTimer *After = NULL);
  void Ins(cTimer *Timer, cTimer *Before = NULL);
  void Del(cTimer *Timer, bool DeleteObject = true);
  };

extern cTimers Timers;

class cSortedTimers : public cVector<const cTimer *> {
public:
  cSortedTimers(void);
  };

#endif //__TIMERS_H
