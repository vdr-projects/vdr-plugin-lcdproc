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
#include "lcd.h"

static const char *VERSION        = "0.0.3";
static const char *MAINMENUENTRY  = NULL;
#ifdef  LCD_hd44780
static const char *DESCRIPTION    = "LCDproc using hd44780 output-mapping";
#endif
#ifdef  LCD_CFontz
static const char *DESCRIPTION    = "LCDproc using CFontz output-mapping";
#endif
#ifdef  LCD_nomap
static const char *DESCRIPTION    = "LCDproc using no output-mapping";
#endif

cLcd *LCDproc = new cLcd;
bool replaymode=false;
bool menumode=false;
bool switched=false;
bool group=false;
char tempstringbuffer[80];
char *LCDprocHOST=LCDHOST;
unsigned int LCDprocPORT=LCDPORT;

// ---

class cLcdFeed : public cStatus {
protected:
  virtual void ChannelSwitch(const cDevice *Device, int ChannelNumber);
  virtual void Recording(const cDevice *Device, const char *Name);
  virtual void Replaying(const cControl *DvbPlayerControl, const char *Name);
  virtual void SetVolume(int Volume, bool Absolute);
  virtual void OsdClear(void);
  virtual void OsdTitle(const char *Title);
  virtual void OsdStatusMessage(const char *Message);
  virtual void OsdHelpKeys(const char *Red, const char *Green, const char *Yellow, const char *Blue);
  virtual void OsdCurrentItem(const char *Text);
  virtual void OsdTextItem(const char *Text, bool Scroll);
  virtual void OsdChannel(const char *Text);
  virtual void OsdProgramme(time_t PresentTime, const char *PresentTitle, const char *PresentSubtitle, time_t FollowingTime, const char *FollowingTitle, const char *FollowingSubtitle);
  };


void cLcdFeed::ChannelSwitch(const cDevice *Device, int ChannelNumber)
{
  syslog(LOG_INFO, "lcdproc: cLcdFeed::ChannelSwitch  %d %d", Device->CardIndex(), ChannelNumber);
  if (ChannelNumber) { 
    LCDproc->SetLine(1,2," "); 
    LCDproc->SetLine(1,3," ");
    LCDproc->SetRunning(false,"No EPG info available.\0", NULL);  // XXX tr !!! 
    switched = true; 
  } else switched = false;
  if (Device) LCDproc->SetPrimaryDevice( (cDevice *) Device );
}

void cLcdFeed::Recording(const cDevice *Device, const char *Name)
{
  syslog(LOG_INFO, "lcdproc: cLcdFeed::Recording  %d %s", Device->CardIndex(), Name);
  if (Name)
    LCDproc->SetCardStat(Device->CardIndex(),2);
  else
    LCDproc->SetCardStat(Device->CardIndex(),1);	  
}

void cLcdFeed::Replaying(const cControl *DvbPlayerControl, const char *Name)
{
  syslog(LOG_INFO, "lcdproc: cLcdFeed::Replaying %s",  Name);
  replaymode=(Name)?true:false;
  if ( replaymode ) {
    LCDproc->SetReplayDevice( (cDvbPlayerControl *) DvbPlayerControl);
    LCDproc->SetMain(2, Name);
    LCDproc->SetThreadState( (cLcd::ThreadStates) 2); // Replaying
  } else {
    LCDproc->SetReplayDevice(NULL); 
    LCDproc->SetProgress(NULL);
    LCDproc->SetLineC(1,1,tempstringbuffer); 
    LCDproc->SetThreadState( (cLcd::ThreadStates) 1); // Title
  }
  menumode=false; 
}

void cLcdFeed::SetVolume(int Volume, bool Absolute)
{
  syslog(LOG_INFO, "lcdproc: cLcdFeed::SetVolume  %d %d", Volume, Absolute);
  LCDproc->ShowVolume( Volume, Absolute );
}

void cLcdFeed::OsdClear(void)
{
  syslog(LOG_INFO, "lcdproc: cLcdFeed::OsdClear");
	  
  if (replaymode)
    LCDproc->SetThreadState( (cLcd::ThreadStates) 2); // Replaying
  else 
    LCDproc->SetThreadState( (cLcd::ThreadStates) 1); // Title
  menumode=false;
  if (group) {LCDproc->SetLineC(1,1,tempstringbuffer); group=false; } 
}

void cLcdFeed::OsdTitle(const char *Title)
{
  syslog(LOG_INFO, "lcdproc: cLcdFeed::OsdTitle '%s'", Title);
  LCDproc->Clear(0);
  LCDproc->SetTitle(Title);
  LCDproc->SetThreadState( (cLcd::ThreadStates) 0); // MENU
  menumode=true;
}

void cLcdFeed::OsdStatusMessage(const char *Message)
{
  syslog(LOG_INFO, "lcdproc: cLcdFeed::OsdStatusMessage '%s'", Message);
  if ( Message ) 
    if ( menumode ) 
      LCDproc->SetStatus(Message);
    else 
      LCDproc->SetWarning(Message);
}

void cLcdFeed::OsdHelpKeys(const char *Red, const char *Green, const char *Yellow, const char *Blue)
{
  syslog(LOG_INFO, "lcdproc: cLcdFeed::OsdHelpKeys %s - %s - %s - %s", Red, Green, Yellow, Blue);
  LCDproc->SetHelp(0,Red, Green, Yellow, Blue);
}

void cLcdFeed::OsdCurrentItem(const char *Text)
{
  syslog(LOG_INFO, "lcdproc: cLcdFeed::OsdCurrentItem %s", Text);
  LCDproc->SetMain(0,Text);
}

void cLcdFeed::OsdTextItem(const char *Text, bool Scroll)
{
  syslog(LOG_INFO, "lcdproc: cLcdFeed::OsdTextItem %s %d", Text, Scroll);
  if (Text) 
    LCDproc->SummaryInit( (char *) Text);
  else if (Scroll) 
      LCDproc->SummaryUp();
    else
      LCDproc->SummaryDown();	    
  LCDproc->SummaryDisplay();
}

void cLcdFeed::OsdChannel(const char *Text)
{
  syslog(LOG_INFO, "lcdproc: cLcdFeed::OsdChannel %s", Text);
  LCDproc->SetLineC(1,1,Text);

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
  syslog(LOG_INFO, "lcdproc: cLcdFeed::OsdProgramme");
  strftime(buffer, sizeof(buffer), "%R", localtime_r(&PresentTime, &tm_r));
  syslog(LOG_INFO, "%5s %s", buffer, PresentTitle);
  syslog(LOG_INFO, "%5s %s", "", PresentSubtitle);
  

  if ( (!isempty(PresentTitle)) && (!isempty(PresentSubtitle)) )
    LCDproc->SetRunning(false,buffer,PresentTitle,PresentSubtitle);
  else if (!isempty(PresentTitle)) LCDproc->SetRunning(false,buffer,PresentTitle);
  else LCDproc->SetRunning(false,"No EPG info available.\0", NULL);  // XXX tr !!!


  strftime(buffer, sizeof(buffer), "%R", localtime_r(&FollowingTime, &tm_r));
  syslog(LOG_INFO, "%5s %s", buffer, FollowingTitle);
  syslog(LOG_INFO, "%5s %s", "", FollowingSubtitle);
  
  if ( (!isempty(FollowingTitle)) && (!isempty(FollowingSubtitle)) )
    LCDproc->SetRunning(true,buffer,FollowingTitle,FollowingSubtitle);
  else if (!isempty(FollowingTitle)) LCDproc->SetRunning(true,buffer,FollowingTitle);
  else LCDproc->SetRunning(true,"No EPG info available.\0", NULL); // XXX tr !!! 
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
  virtual const char *Description(void) { return DESCRIPTION; }
  virtual const char *CommandLineHelp(void);
  virtual bool ProcessArgs(int argc, char *argv[]);
  virtual bool Start(void);
  virtual void Housekeeping(void);
  virtual const char *MainMenuEntry(void) { return MAINMENUENTRY; }
  virtual cOsdMenu *MainMenuAction(void);
  virtual cMenuSetupPage *SetupMenu(void);
  virtual bool SetupParse(const char *Name, const char *Value);
  };

cPluginLcd::cPluginLcd(void)
{
  // Initialize any member varaiables here.
  // DON'T DO ANYTHING ELSE THAT MAY HAVE SIDE EFFECTS, REQUIRE GLOBAL
  // VDR OBJECTS TO EXIST OR PRODUCE ANY OUTPUT!
  lcdFeed = NULL;
  
}

cPluginLcd::~cPluginLcd()
{
  // Clean up after yourself!
  delete lcdFeed;
  delete LCDproc;
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
  if ( LCDproc->Connect(LCDprocHOST,LCDprocPORT) ) {
    syslog(LOG_INFO, "connection to LCDd at %s:%d established.",LCDprocHOST,LCDprocPORT);
    return true;
  }
  return false; 
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

cMenuSetupPage *cPluginLcd::SetupMenu(void)
{
  // Return a setup menu in case the plugin supports one.
  return NULL;
}

bool cPluginLcd::SetupParse(const char *Name, const char *Value)
{
  // Parse your own setup parameters and store their values.
  return false;
}

VDRPLUGINCREATOR(cPluginLcd); // Don't touch this!
