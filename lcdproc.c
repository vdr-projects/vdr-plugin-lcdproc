/*
 * lcdproc.c: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include <getopt.h>
#include <ctype.h>
#include <vdr/plugin.h>
#include <vdr/status.h>
#include <vdr/recording.h>
#include "setup.h"
#include "lcd.h"
#include "lcdtranstbl.h"

static const char *VERSION        = "0.0.10-jw9";
static const char *MAINMENUENTRY  = NULL;
static const char *DESCRIPTION    = trNOOP("LCDproc output");

cLcd *LCDproc;
bool replaymode=false;
bool menumode=false;
bool switched=false;
bool textitem=false;
bool group=false;
char tempstringbuffer[80];
const char *LCDprocHOST=LCDHOST;
unsigned int LCDprocPORT=LCDPORT;

static const char * OutputFunctionText[]= {"Off",
                                           "On",
                                           "Recording DVB1",
                                           "Recording DVB2",
                                           "Recording DVB3",
                                           "Recording DVB4",
                                           "Replay",
                                           "DVD",
                                           "MPlayer",
                                           "MP3",
                                           "MPlayer + MP3",
                                           "User1",
                                           "User2",
                                           "User3"};

class cLcdFeed : public cStatus {
public:
	cLcdFeed() {
		AudioTrack = NULL;
	}
private:
	char * AudioTrack;
protected:
#if VDRVERSNUM < 10726
  virtual void ChannelSwitch(const cDevice *Device, int ChannelNumber);
#else
  virtual void ChannelSwitch(const cDevice *Device, int ChannelNumber, bool LiveView);
#endif
  virtual void Recording(const cDevice *Device, const char *Name, const char *FileName, bool On);
  virtual void Replaying(const cControl *DvbPlayerControl, const char *Name, const char *FileName, bool On);
  virtual void SetVolume(int Volume, bool Absolute);
  virtual void SetAudioTrack(int Index, const char * const *Tracks);
  virtual void SetAudioChannel(int AudioChannel);
  virtual void OsdClear(void);
  virtual void OsdTitle(const char *Title);
  virtual void OsdStatusMessage(const char *Message);
  virtual void OsdHelpKeys(const char *Red, const char *Green, const char *Yellow, const char *Blue);
  virtual void OsdCurrentItem(const char *Text);
  virtual void OsdTextItem(const char *Text, bool Scroll);
  virtual void OsdChannel(const char *Text);
  virtual void OsdProgramme(time_t PresentTime, const char *PresentTitle, const char *PresentSubtitle, time_t FollowingTime, const char *FollowingTitle, const char *FollowingSubtitle);
  };

#if VDRVERSNUM < 10726
void cLcdFeed::ChannelSwitch(const cDevice *Device, int ChannelNumber)
{
  if (Device && Device->IsPrimaryDevice()) {
#else
void cLcdFeed::ChannelSwitch(const cDevice *Device, int ChannelNumber, bool LiveView)
{
  if  (LiveView) {
#endif
    if (ChannelNumber) {
      LCDproc->SetLine(1,2," ");
      LCDproc->SetLine(1,3," ");
      LCDproc->SetRunning(false,tr("Waiting for EPG info."), NULL);
      LCDproc->ChannelSwitched();
      switched = true;
    } else switched = false;
    LCDproc->SetPrimaryDevice( (cDevice *) Device );
  }
}

void cLcdFeed::Recording(const cDevice *Device, const char *Name, const char *FileName, bool On)
{
  //syslog(LOG_INFO, "lcdproc: cLcdFeed::Recording  %d %s", Device->CardIndex(), Name);
  if (On)
    LCDproc->SetCardStat(Device->CardIndex(),2);
  else
    LCDproc->SetCardStat(Device->CardIndex(),1);
}

void cLcdFeed::Replaying(const cControl *DvbPlayerControl, const char *Name, const char *FileName, bool On)
{
  //syslog(LOG_INFO, "lcdproc: cLcdFeed::Replaying %s",  Name);
  replaymode=On;
  if (replaymode) {
    LCDproc->SetReplayDevice( (cDvbPlayerControl *) DvbPlayerControl);
    LCDproc->SetMain(2, Name);
    LCDproc->SetThreadState( (cLcd::ThreadStates) 2); // Replaying
  } else {
    LCDproc->SetReplayDevice(NULL);
    LCDproc->SetProgress(NULL);
    LCDproc->SetLineC(1,LcdSetup.ShowTime?1:0,tempstringbuffer);
    LCDproc->SetThreadState( (cLcd::ThreadStates) 1); // Title
  }
  menumode=false;
}

void cLcdFeed::SetVolume(int Volume, bool Absolute)
{
  //syslog(LOG_INFO, "lcdproc: cLcdFeed::SetVolume  %d %d", Volume, Absolute);
  LCDproc->ShowVolume( Volume, Absolute );
}

void cLcdFeed::SetAudioTrack(int Index, const char * const *Tracks)
{
	OsdTitle(trVDR("Button$Audio"));
	if (AudioTrack)
		free(AudioTrack);
	if (asprintf(&AudioTrack, "%s", Tracks[Index]) < 0) {
		syslog(LOG_ERR, "lcdproc: error allocating memory in asprintf");
		return;
	}
	OsdCurrentItem(AudioTrack);
}

void cLcdFeed::SetAudioChannel(int AudioChannel){
	char * TrackDescription;
	switch (AudioChannel){
	case 0:
		if (asprintf(&TrackDescription, "%s (%s)", AudioTrack, tr("Stereo")) < 0) {
			syslog(LOG_ERR, "lcdproc: error allocating memory in asprintf");
			return;
		}
		break;
	case 1:
		if (asprintf(&TrackDescription, "%s (%s)", AudioTrack, tr("Left channel")) < 0) {
			syslog(LOG_ERR, "lcdproc: error allocating memory in asprintf");
			return;
		}
		break;
	case 2:
		if (asprintf(&TrackDescription, "%s (%s)", AudioTrack, tr("Right channel")) < 0) {
			syslog(LOG_ERR, "lcdproc: error allocating memory in asprintf");
			return;
		}
		break;
	default:
		return;
	}
	OsdCurrentItem(TrackDescription);
	free(TrackDescription);
}
void cLcdFeed::OsdClear(void)
{
  //syslog(LOG_INFO, "lcdproc: cLcdFeed::OsdClear");

  if (replaymode)
    LCDproc->SetThreadState( (cLcd::ThreadStates) 2); // Replaying
  else
    LCDproc->SetThreadState( (cLcd::ThreadStates) 1); // Title
  menumode=false;
  if (group) {LCDproc->SetLineC(1,LcdSetup.ShowTime?1:0,tempstringbuffer); group=false; }
}

void cLcdFeed::OsdTitle(const char *Title)
{
  //syslog(LOG_INFO, "lcdproc: cLcdFeed::OsdTitle '%s'", Title);
  if ( ! textitem ) {
    LCDproc->Clear(0);
    LCDproc->SetTitle(Title);
    LCDproc->SetThreadState( (cLcd::ThreadStates) 0); // MENU
  }
  menumode=true; textitem=false;
}

void cLcdFeed::OsdStatusMessage(const char *Message)
{
	//syslog(LOG_INFO, "lcdproc: cLcdFeed::OsdStatusMessage '%s'", Message);
	if ( Message ) {
		if ( menumode )
			LCDproc->SetStatus(Message);
		else
			LCDproc->SetWarning(Message);
	}
	else {
		OsdClear( );
	}
}

void cLcdFeed::OsdHelpKeys(const char *Red, const char *Green, const char *Yellow, const char *Blue)
{
  //syslog(LOG_INFO, "lcdproc: cLcdFeed::OsdHelpKeys %s - %s - %s - %s", Red, Green, Yellow, Blue);
  LCDproc->SetHelp(0,Red, Green, Yellow, Blue);
}

void cLcdFeed::OsdCurrentItem(const char *Text)
{
  //syslog(LOG_INFO, "lcdproc: cLcdFeed::OsdCurrentItem %s", Text);
  LCDproc->SetMain(0,Text);
}

void cLcdFeed::OsdTextItem(const char *Text, bool Scroll)
{
  //syslog(LOG_INFO, "lcdproc: cLcdFeed::OsdTextItem %s %d", Text, Scroll);
  if (Text)
    LCDproc->SummaryInit( (char *) Text);
  else if (Scroll)
      LCDproc->SummaryUp();
    else
      LCDproc->SummaryDown();
  LCDproc->SetThreadState( (cLcd::ThreadStates) 3); // MISC
  LCDproc->SummaryDisplay();
}

void cLcdFeed::OsdChannel(const char *Text)
{
  //syslog(LOG_INFO, "lcdproc: cLcdFeed::OsdChannel %s", Text);
  LCDproc->SetLineC(1,LcdSetup.ShowTime?1:0,Text);

  bool switching = group = !isdigit(Text[0]);
  if (!group) strcpy(tempstringbuffer,Text);
  for (unsigned int i=0; ( i<strlen(Text)-1 ) && !switching  ; i++) {
     switching= isdigit(Text[i]) && Text[i+1]=='-'  ;
  }

  if (switched || switching)
    LCDproc->SetThreadState( (cLcd::ThreadStates) 1); // TITLE
  else
    LCDproc->SetThreadState( (cLcd::ThreadStates) 3); // MISC

  switched=false; menumode=false;
}

void cLcdFeed::OsdProgramme(time_t PresentTime, const char *PresentTitle, const char *PresentSubtitle, time_t FollowingTime, const char *FollowingTitle, const char *FollowingSubtitle)
{
  char buffer[25];
  struct tm tm_r;
  //syslog(LOG_INFO, "lcdproc: cLcdFeed::OsdProgramme");
  strftime(buffer, sizeof(buffer), "%R", localtime_r(&PresentTime, &tm_r));
  //syslog(LOG_INFO, "%5s %s", buffer, PresentTitle);
  //syslog(LOG_INFO, "%5s %s", "", PresentSubtitle);


  if ( (LcdSetup.ShowSubtitle) && (!isempty(PresentTitle)) && (!isempty(PresentSubtitle)) )
    LCDproc->SetRunning(false,buffer,PresentTitle,PresentSubtitle);
  else if (!isempty(PresentTitle)) LCDproc->SetRunning(false,buffer,PresentTitle);
  else LCDproc->SetRunning(false,tr("No EPG info available."), NULL);


  strftime(buffer, sizeof(buffer), "%R", localtime_r(&FollowingTime, &tm_r));
  //syslog(LOG_INFO, "%5s %s", buffer, FollowingTitle);
  //syslog(LOG_INFO, "%5s %s", "", FollowingSubtitle);

  if ( (LcdSetup.ShowSubtitle) && (!isempty(FollowingTitle)) && (!isempty(FollowingSubtitle)) )
    LCDproc->SetRunning(true,buffer,FollowingTitle,FollowingSubtitle);
  else if (!isempty(FollowingTitle)) LCDproc->SetRunning(true,buffer,FollowingTitle);
  else LCDproc->SetRunning(true,tr("No EPG info available."), NULL);
}

// ---

class cPluginLcd : public cPlugin {
private:
  // Add any member variables or functions you may need here.
  cLcdFeed *lcdFeed;
public:
  cPluginLcd(void);
  virtual ~cPluginLcd();
  virtual const char *Version(void) { return VERSION; }
  virtual const char *Description(void) { return tr(DESCRIPTION); }
  virtual const char *CommandLineHelp(void);
  virtual bool ProcessArgs(int argc, char *argv[]);
  virtual bool Start(void);
  virtual void Stop(void);
  virtual void Housekeeping(void);
  virtual const char *MainMenuEntry(void) { return MAINMENUENTRY; }
  virtual cOsdMenu *MainMenuAction(void);
  virtual cMenuSetupPage *SetupMenu(void);
  virtual bool SetupParse(const char *Name, const char *Value);
  virtual const char **SVDRPHelpPages(void);
  virtual cString SVDRPCommand(const char *Command, const char *Option, int &ReplyCode);
  };

cPluginLcd::cPluginLcd(void)
{
  // Initialize any member varaiables here.
  // DON'T DO ANYTHING ELSE THAT MAY HAVE SIDE EFFECTS, REQUIRE GLOBAL
  // VDR OBJECTS TO EXIST OR PRODUCE ANY OUTPUT!
  lcdFeed = NULL;
  LCDproc = NULL;
}

cPluginLcd::~cPluginLcd()
{
  // Clean up after yourself!
  delete lcdFeed;
}

const char *cPluginLcd::CommandLineHelp(void)
{
  // Return a string that describes all known command line options.
  return "  -h <host>, --host <host>  LCDproc host (default=localhost)\n  -p <port>, --port <port>  LCDproc port (default=13666)\n";

}

bool cPluginLcd::ProcessArgs(int argc, char *argv[])
{
  // Implement command line argument processing here if applicable.

  static struct option long_options[] = {
       { "host",      required_argument, NULL, 'h' },
       { "port",      required_argument, NULL, 'p' },
       { NULL }
     };

  int c;
  while ((c = getopt_long(argc, argv, "h:p:", long_options, NULL)) != -1) {
        switch (c) {
          case 'h': LCDprocHOST=optarg;
                    break;
          case 'p': LCDprocPORT=atoi(optarg);
                    break;
          default:  return false;
          }
        }
  return true;
}

bool cPluginLcd::Start(void)
{
  // Start any background activities the plugin shall perform.
  lcdFeed = new cLcdFeed;
  LCDproc = new cLcd;
  if ( LCDproc->Connect(LCDprocHOST,LCDprocPORT) ) {
    syslog(LOG_INFO, "LCDproc-Plugin started at %s:%d.",LCDprocHOST,LCDprocPORT);
    return true;
  }
  syslog(LOG_INFO, "connection to LCDd at %s:%d failed.",LCDprocHOST,LCDprocPORT);
  return false;
}

void cPluginLcd::Stop(void)
{
  delete LCDproc;
}

void cPluginLcd::Housekeeping(void)
{
  // Perform any cleanup or other regular tasks.
}

cOsdMenu *cPluginLcd::MainMenuAction(void)
{
  // Perform the action when selected from the main VDR menu.
  return NULL;
}

// --- cMenuEditStraTrItem -----------------------------------------------------

class cMenuEditStraTrItem : public cMenuEditIntItem {
private:
  const char * const *strings;
protected:
  virtual void Set(void);
public:
  cMenuEditStraTrItem(const char *Name, int *Value, int NumStrings, const char * const *Strings);
  };

cMenuEditStraTrItem::cMenuEditStraTrItem(const char *Name, int *Value, int NumStrings, const char * const *Strings)
:cMenuEditIntItem(Name, Value, 0, NumStrings - 1)
{
  strings = Strings;
  Set();
}

void cMenuEditStraTrItem::Set(void)
{
  SetValue(strings[*value]);
}

// --- cMenuSetupLcd -----------------------------------------------------------

class cMenuSetupLcd : public cMenuSetupPage {
  private:
    cLcdSetup newLcdSetup;
    const char * RecordingStatusText[2];
    const char * PrioBackFunctionText[3];
  protected:
     virtual void Store(void);
  public:
     cMenuSetupLcd(void);
};

cMenuSetupLcd::cMenuSetupLcd(void)
{
  PrioBackFunctionText[0] = tr("off");
  PrioBackFunctionText[1] = tr("on");
  PrioBackFunctionText[2] = tr("auto");
  RecordingStatusText[0] = tr("detailed");
  RecordingStatusText[1] = tr("simple");
  char str2[30];
  newLcdSetup=LcdSetup;
  Add(new cMenuEditIntItem( tr("FullCycle"),           &newLcdSetup.FullCycle,1,999));
  Add(new cMenuEditIntItem( tr("TimeDateCycle"),       &newLcdSetup.TimeCycle,0,LcdSetup.FullCycle));
  Add(new cMenuEditIntItem( tr("VolumeKeep"),          &newLcdSetup.VolumeKeep,0,999));
  Add(new cMenuEditIntItem( tr("Scrollwait"),          &newLcdSetup.Scrollwait,1,999));
  Add(new cMenuEditIntItem( tr("Scrollspeed"),         &newLcdSetup.Scrollspeed,1,999));
  Add(new cMenuEditIntItem( tr("Charmap"),             &newLcdSetup.Charmap,0,LCDMAXTRANSTBL-1 ));
  Add(new cMenuEditBoolItem( tr("AltShift"),           &newLcdSetup.AltShift));
  Add(new cMenuEditStraItem( tr("BackLight"),          &newLcdSetup.BackLight, 3, PrioBackFunctionText));
  Add(new cMenuEditStraItem( tr("SetClientPriority"),  &newLcdSetup.SetPrio, 3, PrioBackFunctionText));
  Add(new cMenuEditIntItem( tr("NormalClientPriority"),&newLcdSetup.ClientPrioN,0,255));
  Add(new cMenuEditIntItem( tr("HighClientPriority"),  &newLcdSetup.ClientPrioH,0,255));
  Add(new cMenuEditIntItem( tr("BackLightWait"),       &newLcdSetup.BackLightWait,1,999));
  Add(new cMenuEditIntItem( tr("PrioWait"),            &newLcdSetup.PrioWait,1,999));
  Add(new cMenuEditIntItem( tr("OutputNumber"),        &newLcdSetup.OutputNumber));
  for (int i =0 ; i <  newLcdSetup.OutputNumber; i++){
    sprintf(str2,"%s %d",tr("OutputNumber"),i);
    Add(new cMenuEditStraTrItem( str2, &newLcdSetup.OutputFunction[i],14, OutputFunctionText));
  }
  Add(new cMenuEditStraItem( tr("Recording status"),   &newLcdSetup.RecordingStatus, 2, RecordingStatusText));
  Add(new cMenuEditBoolItem( tr("Show time"),          &newLcdSetup.ShowTime));
  Add(new cMenuEditBoolItem( tr("Show subtitle"),      &newLcdSetup.ShowSubtitle));
}

void cMenuSetupLcd::Store(void)
{
  char str2[30];
  SetupStore("FullCycle",   LcdSetup.FullCycle   = newLcdSetup.FullCycle);
  SetupStore("TimeCycle",   LcdSetup.TimeCycle   = newLcdSetup.TimeCycle);
  SetupStore("VolumeKeep",  LcdSetup.VolumeKeep  = newLcdSetup.VolumeKeep);
  SetupStore("Scrollwait",  LcdSetup.Scrollwait  = newLcdSetup.Scrollwait);
  SetupStore("Scrollspeed", LcdSetup.Scrollspeed = newLcdSetup.Scrollspeed);
  SetupStore("Charmap",     LcdSetup.Charmap     = newLcdSetup.Charmap);
  SetupStore("AltShift",    LcdSetup.AltShift    = newLcdSetup.AltShift);
  SetupStore("BackLight",   LcdSetup.BackLight   = newLcdSetup.BackLight);
  SetupStore("SetPrio",     LcdSetup.SetPrio     = newLcdSetup.SetPrio);
  SetupStore("ClientPrioN", LcdSetup.ClientPrioN = newLcdSetup.ClientPrioN);
  SetupStore("ClientPrioH", LcdSetup.ClientPrioH = newLcdSetup.ClientPrioH);
  SetupStore("BackLightWait", LcdSetup.BackLightWait = newLcdSetup.BackLightWait);
  SetupStore("PrioWait",    LcdSetup.PrioWait = newLcdSetup.PrioWait);
  SetupStore("OutputNumber",LcdSetup.OutputNumber   = newLcdSetup.OutputNumber);
  for (int i =0 ; i <  newLcdSetup.OutputNumber; i++){
    sprintf(str2,"OutputNumber %d",i);
    SetupStore(str2,   LcdSetup.OutputFunction[i]   = newLcdSetup.OutputFunction[i]);
  }
  SetupStore("RecordingStatus", LcdSetup.RecordingStatus = newLcdSetup.RecordingStatus);
  SetupStore("ShowTime",        LcdSetup.ShowTime        = newLcdSetup.ShowTime);
  SetupStore("ShowSubtitle",    LcdSetup.ShowSubtitle    = newLcdSetup.ShowSubtitle);
}


cMenuSetupPage *cPluginLcd::SetupMenu(void)
{
  // Return a setup menu in case the plugin supports one.
  return new cMenuSetupLcd;
}

bool cPluginLcd::SetupParse(const char *Name, const char *Value)
{
  // Parse your own setup parameters and store their values.
  if      (!strcasecmp(Name, "FullCycle"))    LcdSetup.FullCycle   = atoi(Value);
  else if (!strcasecmp(Name, "TimeCycle"))    LcdSetup.TimeCycle   = atoi(Value);
  else if (!strcasecmp(Name, "VolumeKeep"))   LcdSetup.VolumeKeep  = atoi(Value);
  else if (!strcasecmp(Name, "Scrollwait"))   LcdSetup.Scrollwait  = atoi(Value);
  else if (!strcasecmp(Name, "Scrollspeed"))  LcdSetup.Scrollspeed = atoi(Value);
  else if (!strcasecmp(Name, "Charmap"))      LcdSetup.Charmap     = atoi(Value);
  else if (!strcasecmp(Name, "AltShift"))     LcdSetup.AltShift    = atoi(Value);
  else if (!strcasecmp(Name, "BackLight"))    LcdSetup.BackLight   = atoi(Value);
  else if (!strcasecmp(Name, "SetPrio"))      LcdSetup.SetPrio     = atoi(Value);
  else if (!strcasecmp(Name, "ClientPrioN"))  LcdSetup.ClientPrioN = atoi(Value);
  else if (!strcasecmp(Name, "ClientPrioH"))  LcdSetup.ClientPrioH = atoi(Value);
  else if (!strcasecmp(Name, "BackLightWait"))  LcdSetup.BackLightWait = atoi(Value);
  else if (!strcasecmp(Name, "PrioWait"))       LcdSetup.PrioWait = atoi(Value);
  else if (!strcasecmp(Name, "OutputNumber"))   LcdSetup.OutputNumber        = atoi(Value);
  else if (!strcasecmp(Name, "OutputNumber 0")) LcdSetup.OutputFunction[0]   = atoi(Value);
  else if (!strcasecmp(Name, "OutputNumber 1")) LcdSetup.OutputFunction[1]   = atoi(Value);
  else if (!strcasecmp(Name, "OutputNumber 2")) LcdSetup.OutputFunction[2]   = atoi(Value);
  else if (!strcasecmp(Name, "OutputNumber 3")) LcdSetup.OutputFunction[3]   = atoi(Value);
  else if (!strcasecmp(Name, "OutputNumber 4")) LcdSetup.OutputFunction[4]   = atoi(Value);
  else if (!strcasecmp(Name, "OutputNumber 5")) LcdSetup.OutputFunction[5]   = atoi(Value);
  else if (!strcasecmp(Name, "OutputNumber 6")) LcdSetup.OutputFunction[6]   = atoi(Value);
  else if (!strcasecmp(Name, "OutputNumber 7")) LcdSetup.OutputFunction[7]   = atoi(Value);
  else if (!strcasecmp(Name, "OutputNumber 8")) LcdSetup.OutputFunction[8]   = atoi(Value);
  else if (!strcasecmp(Name, "OutputNumber 9")) LcdSetup.OutputFunction[9]   = atoi(Value);
  else if (!strcasecmp(Name, "RecordingStatus")) LcdSetup.RecordingStatus    = atoi(Value);
  else if (!strcasecmp(Name, "ShowTime"))       LcdSetup.ShowTime            = atoi(Value);
  else if (!strcasecmp(Name, "ShowSubtitle"))   LcdSetup.ShowSubtitle        = atoi(Value);
  else
  return false;
  return true;
}

const char **cPluginLcd::SVDRPHelpPages(void)
{
  // Return help text for SVDRP commands this plugin implements
  static const char *HelpPages[] = {
    "ON\n"
    "    Connect to the LCDd socket.",
    "OFF\n"
    "    Disconnect from the LCDd socket.",
    NULL
    };
  return HelpPages;
}

cString cPluginLcd::SVDRPCommand(const char *Command, const char *Option, int &ReplyCode)
{
  // Process SVDRP commands this plugin implements
  if(!strcasecmp(Command, "ON")) {

    if(LCDproc->Resume())
      return "connection to LCDd resumed";
    else
      return "connection to LCDd not suspended";

  } else if(!strcasecmp(Command, "OFF")) {
    if(LCDproc->Suspend())
      return "connection to LCDd suspended";
    else
      return "connection to LCDd already suspended";

  } else { ReplyCode=501; return "Missing or unkown args"; }

  return NULL;
}

VDRPLUGINCREATOR(cPluginLcd); // Don't touch this!
