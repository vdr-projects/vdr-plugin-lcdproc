#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <vdr/config.h>
#include <vdr/i18n.h>
#include <vdr/tools.h>
#include "lcd.h"
#include "sockets.h"
#include "lcdkeyconf.h"

// character mapping for output, see cLcd::Write
#ifdef  LCD_hd44780
#include "lcdtranstbl-hd44780.h" // hd44780 output 
#endif
#ifdef  LCD_CFontz
#include "lcdtranstbl-CFontz.h" // CFontz output thanks to Stephan Schreiber. 
#endif
#ifdef  LCD_nomap
#include "lcdtranstbl-nomap.h" // unmapped output (usable for curses output) 
#endif

#define LCDMENU   0
#define LCDTITLE  1
#define LCDREPLAY 2
#define LCDMISC   3
#define LCDVOL    4

// public:

cLcd::cLcd() {
  int i,j;
  connected=false; ThreadStateData.showvolume=false; ThreadStateData.newscroll=false; sock=wid=hgt=cellwid=cellhgt=0;
  replayDvbApi=NULL; primaryDvbApi=NULL;
  
  for (i=0;i<LCDMAXSTATES;i++) for (j=0;j<4;j++) { 
    ThreadStateData.lcdbuffer[i][j][0]=' '; 
    ThreadStateData.lcdbuffer[i][j][1]='\0'; 
    ThreadStateData.lcddirty[i][j]=true; 
  }   
  ThreadStateData.State=Menu;
  for (i=0;i<LCDMAXSTATEBUF;i++) LastState[i]=Title; LastStateP=0;
  ThreadStateData.barx=1, ThreadStateData.bary=1, ThreadStateData.barl=0; 
  for (i=0;i<LCDMAXCARDS;i++) ThreadStateData.CardStat[i]=0;
}

cLcd::~cLcd() {
  if (connected) { /*cLcd::Stop();*/  cLcd::Close(); }   //YYYY
}

bool cLcd::Connect( char *host, unsigned int port ) {

  char istring[1024];
  unsigned int i=0;

  if ( ( sock = sock_connect(host,port) ) < 1) {
    return connected=false;
  }

  sock_send_string(sock, "hello\n"); 
  usleep(500000); // wait for a connect message  
  sock_recv(sock, istring, 1024);

  if ( strncmp("connect LCDproc",istring,15) != 0 ) { cLcd::Close(); return connected=false; }

  while ( (strncmp("lcd",istring+i,3) != 0 ) && (i<(strlen(istring)-5)) ) i++;  

  if (sscanf( istring+i,"lcd wid %d hgt %d cellwid %d cellhgt %d" 
      , &wid, &hgt, &cellwid, &cellhgt) ) connected=true;

  if ((hgt < 4 ) || (wid < 16)) connected = false; // 4 lines are needed, may work with more than 4 though
  if ( (hgt==2) && (wid>31) ) { connected = true; wid=wid/2; LineMode=0; }   // 2x40 
  if (!connected) { cLcd::Close(); return connected; }

  sock_send_string(sock,"screen_add VDR\n"); sock_recv(sock, istring, 1024);
  sock_send_string(sock,"screen_set VDR -heartbeat off\n"); sock_recv(sock, istring, 1024);
  sock_send_string(sock,"widget_add VDR line1 string\n"); sock_recv(sock, istring, 1024);
  sock_send_string(sock,"widget_add VDR line2 string\n"); sock_recv(sock, istring, 1024);
  sock_send_string(sock,"widget_add VDR line3 string\n"); sock_recv(sock, istring, 1024);
  sock_send_string(sock,"widget_add VDR line4 string\n"); sock_recv(sock, istring, 1024);
  sock_send_string(sock,"widget_add VDR prbar hbar\n"); sock_recv(sock, istring, 1024);
  sock_send_string(sock,"widget_set VDR prbar 1 1 0\n"); sock_recv(sock, istring, 1024);
  
  for (i=0; i<LcdMaxKeys;i++) {
    sprintf(istring,"client_add_key %c\n",LcdUsedKeys[i]);
    sock_send_string(sock,istring); 
    sock_recv(sock, istring, 1024);
  }

  cLcd::Start(); SetThreadState(Title); // Start thread
  return connected;
}

void cLcd::Close() {
  sock_close(sock); 
  connected=false; sock=wid=hgt=cellwid=cellhgt=0; 
}

void cLcd::Info() { // just for testing ...
 if (connected)
   printf("sock %d, wid %d, hgt %d, cellwid %d, cellhgt %d\n",sock, wid, hgt, cellwid, cellhgt);
 else 
   printf("not connected\n");
}

void cLcd::SetTitle( const char *string) {
  if (!connected) return;
                        
  unsigned int i;
  char title[wid+1];
  const char *trstring=tr("Schedule - %s");
  int trstringlength=strlen(trstring)-3;

  if (strncmp(trstring,string,trstringlength)==0) {
    title[0]='>'; snprintf( title+1, wid,"%s",string+trstringlength);
  } else if (strlen(string) > (wid-1)) {
    snprintf( title, wid+1,"%s",string);
  } else {
    memset(title,' ',wid/2+1);
    snprintf(title + ( (wid-strlen(string))/2 ),wid-( (wid-strlen(string))/2 ),"%s",string); // center title
    for (i=0;i<strlen(title);i++) title[i]=toupper(title[i]); // toupper
  }
  cLcd::SetLine(LCDMENU,0,title);
}

void cLcd::SetMain( unsigned int n, const char *string) {
  if (!connected) return;
 
  char line2[wid+1];
  char line3[wid+1];

  if (string != NULL) {
    cLcd::Copy(ThreadStateData.lcdfullbuffer[n],string,LCDMAXFULLSTRING-3);
    int i = strlen(ThreadStateData.lcdfullbuffer[n]);
    ThreadStateData.lcdfullbuffer[n][i++]=' ';
    ThreadStateData.lcdfullbuffer[n][i++]='*';
    ThreadStateData.lcdfullbuffer[n][i++]=' ';
    ThreadStateData.lcdfullbuffer[n][i]='\0';
    ThreadStateData.newscroll=true;
    cLcd::Copy(StringBuffer,string,2*wid);
    cLcd::Split(StringBuffer,line2,line3);
  }
  cLcd::SetBuffer(n,NULL,line2,line3,NULL);
}

void cLcd::SetHelp( unsigned int n, const char *Red, const char *Green, const char *Yellow, const char *Blue) {
  if (!connected) return;

  char help[2*wid], red[wid+1], green[wid+1], yellow[wid+1], blue[wid+1];
  unsigned int allchars=0, i, empty=0, spacewid=1;
  char *longest, *longest1, *longest2;

  if ( Red==NULL || Red[0]=='\0' ) { 
    empty++; red[0]=' '; red[1]='\0';
  } else { 
    strncpy(red,Red,wid); allchars+=strlen(red); 
  }
  if ( Green==NULL || Green[0]=='\0' )  { 
    empty++; green[0]=' '; green[1]='\0';
  } else { 
    strncpy(green,Green,wid); allchars+=strlen(green); 
  }
  if ( Yellow==NULL || Yellow[0]=='\0' ) { 
    empty++; yellow[0]=' '; yellow[1]='\0';
  } else { 
    strncpy(yellow,Yellow,wid); allchars+=strlen(yellow); 
  }
  if ( Blue==NULL || Blue[0]=='\0' ) { 
    empty++; blue[0]=' '; blue[1]='\0';
  } else { 
    strncpy(blue,Blue,wid); allchars+=strlen(blue); 
  }

  while (allchars > (wid-empty-3)) {
    longest1=( strlen(red)      > strlen(green)    ) ? red     : green;
    longest2=( strlen(yellow)   > strlen(blue)     ) ? yellow  : blue;
    longest =( strlen(longest1) > strlen(longest2) ) ? longest1: longest2;
    longest[strlen(longest)-1]='\0'; allchars--;
  }

  if (empty>0) {
    spacewid=(wid-allchars-3)/empty;
    if (spacewid<1) spacewid=1;
    char spacer[spacewid+1]; memset(spacer,'-',spacewid); spacer[spacewid]='\0';
    if ( Red==NULL    || Red[0]=='\0' )    strncpy(red,spacer,spacewid+1);
    if ( Green==NULL  || Green[0]=='\0' )  strncpy(green,spacer,spacewid+1);
    if ( Yellow==NULL || Yellow[0]=='\0' ) strncpy(yellow,spacer,spacewid+1);
    if ( Blue==NULL   || Blue[0]=='\0' )   strncpy(blue,spacer,spacewid+1);
  }

  #define MANYBLANKS "%s                                         "

  snprintf(help,wid,MANYBLANKS,red);
  snprintf(help+strlen(red)+1,wid,MANYBLANKS,green);
  snprintf(help+wid-strlen(blue)-strlen(yellow)-1,wid,MANYBLANKS,yellow);
  snprintf(help+wid-strlen(blue),strlen(blue)+1,"%s",blue);
  for (i=0;i<strlen(help);i++) help[i]=toupper(help[i]);
  cLcd::SetLine(n,3,help);
}

void cLcd::SetStatus( const char *string) {
  if (!connected) return;
                   
  char statstring[2*wid+1];

  if (string == NULL) {
    cLcd::SetMain(LCDMENU,StringBuffer);
  } else {
    cLcd::Copy(statstring,string,2*wid);
    cLcd::SetMain(LCDMENU,statstring);
  }
}

void cLcd::SetWarning( const char *string) {
  if (!connected) return;

  char statstring[2*wid+1];

  if (string != NULL) {
    cLcd::Copy(statstring,string,2*wid);
    cLcd::Clear(LCDMISC);
    cLcd::SetMain(LCDMISC,statstring);
    cLcd::SetThreadState(Misc);
  }
}

void cLcd::ShowVolume(unsigned int vol, bool muted ) {
if (!connected) return;
  BeginMutualExclusion();
    ThreadStateData.volume=vol;
    ThreadStateData.muted=muted;
    ThreadStateData.showvolume=true;
  EndMutualExclusion();
  if (ThreadStateData.muted) {
    cLcd::SetLine(Vol,0," ");
    cLcd::SetLine(Vol,1,tr("Mute"));
    cLcd::SetLine(Vol,2," ");
    cLcd::SetLine(Vol,3," ");
  } else {
    if (hgt==2) { 
      cLcd::SetLine(Vol,0,"|---|---|---|---|---|---|---|---|---|---");
      cLcd::SetLine(Vol,3," ");
    } else {
      cLcd::SetLine(Vol,0,tr("Volume "));
      cLcd::SetLine(Vol,3,"|---|---|---|---|---|---|---|---|---|---");
    }  
    cLcd::SetLine(Vol,1,"|---|---|---|---|---|---|---|---|---|---");
    cLcd::SetLine(Vol,2," ");
  }
}

void cLcd::SetProgress(const char *begin, const char *end, int percent) {
  if (!connected) return;

  char workstring[256];

  if (percent>100) percent=100; if (percent<0) percent=0;
  if (begin==NULL) {
    BeginMutualExclusion();
      ThreadStateData.barx=1; ThreadStateData.bary=1; ThreadStateData.barl=0;
    EndMutualExclusion(); 
  } else {
    time_t t = time(NULL);
    if (t != LastProgress) { // output only once a second
      int beginw=strlen(begin);
      int endw=strlen(end);
      sprintf(workstring,"%s", begin);
      memset(workstring+beginw,' ',wid-beginw-endw);
      sprintf(workstring+wid-endw,"%s", end);
      cLcd::SetLine(LCDREPLAY,3,workstring);
      BeginMutualExclusion();
        ThreadStateData.barx=beginw+1+((hgt==2)?wid:0); 
        ThreadStateData.bary=((hgt==2)?1:4); 
        ThreadStateData.barl=(percent*cellwid*(wid-beginw-endw))/100;
      EndMutualExclusion();
      LastProgress = t; 
    }
  } 
}

void cLcd::SetLine(unsigned int n, unsigned int l, const char *string) {
  if (!connected) return;

  BeginMutualExclusion();
    if (string != NULL) strncpy(ThreadStateData.lcdbuffer[n][l],string,wid+1);
    ThreadStateData.lcddirty[n][l]=true;
  EndMutualExclusion();
}

void cLcd::SetLineC(unsigned int n, unsigned int l, const char *string) {
  if (!connected) return;

  BeginMutualExclusion();
    if (string != NULL) cLcd::Copy(ThreadStateData.lcdbuffer[n][l],string,wid);
    ThreadStateData.lcddirty[n][l]=true;
  EndMutualExclusion();
}

void cLcd::SetBuffer(unsigned int n, const char *l1,const char *l2,const char *l3,const char *l4) {
  if (!connected) return;
                         
  BeginMutualExclusion();
    if (l1 != NULL) strncpy(ThreadStateData.lcdbuffer[n][0],l1,wid+1); ThreadStateData.lcddirty[n][0]=true; 
    if (l2 != NULL) strncpy(ThreadStateData.lcdbuffer[n][1],l2,wid+1); ThreadStateData.lcddirty[n][1]=true; 
    if (l3 != NULL) strncpy(ThreadStateData.lcdbuffer[n][2],l3,wid+1); ThreadStateData.lcddirty[n][2]=true; 
    if (l4 != NULL) strncpy(ThreadStateData.lcdbuffer[n][3],l4,wid+1); ThreadStateData.lcddirty[n][3]=true; 
  EndMutualExclusion();
}

void cLcd::SetRunning( bool nownext, const char *string1, const char *string2, const char *string3) {
  if (!connected) return;

  char line[256];
  char line1[256];

  snprintf(line,256,"%s %s%s%s",
    (string1==NULL || string1[0]=='\0')?" ":string1,
    (string2==NULL || string2[0]=='\0')?" ":string2,
    (string3==NULL || string3[0]=='\0')?"":"|",
    (string3==NULL || string3[0]=='\0')?" ":string3);
  cLcd::Copy(line1,line,2*wid);

  if (nownext) {
    cLcd::Split(line1,ThreadStateData.lcdbuffer[LCDMISC][2],ThreadStateData.lcdbuffer[LCDMISC][3]);
    ThreadStateData.lcddirty[LCDMISC][2]=true; ThreadStateData.lcddirty[LCDMISC][3]=true;
  } else {
    BeginMutualExclusion();
      cLcd::Split(line1,ThreadStateData.lcdbuffer[LCDTITLE][2],ThreadStateData.lcdbuffer[LCDTITLE][3]);
      ThreadStateData.lcddirty[LCDTITLE][2]=true; ThreadStateData.lcddirty[LCDTITLE][3]=true;
    EndMutualExclusion();
    cLcd::SetBuffer(LCDMISC,ThreadStateData.lcdbuffer[LCDTITLE][2],ThreadStateData.lcdbuffer[LCDTITLE][3],NULL,NULL);

    cLcd::Copy(ThreadStateData.lcdfullbuffer[LCDTITLE],line,LCDMAXFULLSTRING-3);
    int i = strlen(ThreadStateData.lcdfullbuffer[LCDTITLE]);
    ThreadStateData.lcdfullbuffer[LCDTITLE][i++]=' ';
    ThreadStateData.lcdfullbuffer[LCDTITLE][i++]='*';
    ThreadStateData.lcdfullbuffer[LCDTITLE][i++]=' '; 
    ThreadStateData.lcdfullbuffer[LCDTITLE][i]='\0';  
    ThreadStateData.newscroll=true;
  }
}

void cLcd::SummaryInit(char *string) {
  SummaryText  = string;
  SummaryTextL = strlen(string);
  SummaryCurrent=0;
}

void cLcd::SummaryDown() {
  if (!connected) return;

  SummaryCurrent=( (SummaryCurrent+4*wid) < SummaryTextL )?SummaryCurrent+4*wid:SummaryCurrent; 
}

void cLcd::SummaryUp() {
  if (!connected) return;

  SummaryCurrent=( SummaryCurrent > 4*wid )?SummaryCurrent-4*wid:0;
}

void cLcd::SummaryDisplay() {
  if ( (!connected) || (SummaryTextL < 1) ) return;

  char workstring[256];
  unsigned int i, offset=0;

  for (i=0;i<4;i++) {
    if (SummaryCurrent+offset < SummaryTextL) {
      strncpy(workstring,SummaryText+SummaryCurrent+offset,wid+1);
      workstring[wid]='\0';
      cLcd::SetLine(LCDMENU,i,workstring);
      offset+=wid;
    } else cLcd::SetLine(LCDMENU,i," ");
  }
}

void cLcd::SetCardStat(unsigned int card, unsigned int stat) {
  if (!connected) return;
  BeginMutualExclusion();
    ThreadStateData.CardStat[card]=stat;
  EndMutualExclusion();
}

void cLcd::Clear( unsigned int n) {
  if (!connected) return;

  cLcd::SetBuffer(n," "," "," "," "); 
}

void cLcd::SetThreadState( ThreadStates newstate ) {
  if (!connected) return;

  BeginMutualExclusion();
    if (ThreadStateData.State != newstate) { 
      cLcd::LastState[(++LastStateP)%LCDMAXSTATEBUF]=newstate;
      ThreadStateData.State=newstate; 
      ThreadStateData.barx=1, ThreadStateData.bary=1, ThreadStateData.barl=0; 
    }
  EndMutualExclusion(); 
}

void cLcd::PopThreadState() {
  BeginMutualExclusion();
    LastStateP=(LastStateP+LCDMAXSTATEBUF-1)%LCDMAXSTATEBUF;
    ThreadStateData.State=cLcd::LastState[LastStateP];
    ThreadStateData.barx=1, ThreadStateData.bary=1, ThreadStateData.barl=0;
  EndMutualExclusion();
}

void cLcd::SetReplayDevice(cControl *DvbApi) {
  replayDvbApi=DvbApi;
}	

void cLcd::SetPrimaryDevice(cDevice *DvbApi) {
	  primaryDvbApi=DvbApi;
}


// private:

// everything between BeginMutualExclusion(); and EndMutualExclusion();
// should have exclusive access to ThreadStateData

void cLcd::BeginMutualExclusion() {
  CriticalArea.Lock();
}

void cLcd::EndMutualExclusion() {
  cLcd::CriticalArea.Unlock();
}

void cLcd::Copy(char *to, const char *from, unsigned int max) { // eliminates tabs, double blanks ...

  unsigned int i=0 , out=0;

  if (from != NULL) {
    while ((out < max) && (from[i] != '\0') ) {
      to[out]=(isspace(from[i]))?' ':from[i];
      if ( (out>0) && (to[out-1]==' ') && ispunct(to[out]) ) {to[out-1]=to[out]; to[out]=' '; }
      if (!(( (out==0) && (to[out]==' ') ) ||
            ( (out>0) && (to[out-1]==' ') && (to[out]==' ') ) ||
            ( (out>0) && ispunct(to[out-1]) && (to[out]==' ') )
            )) out++;
      i++;
    }
    if (out==0) to[out++]=' ';
    to[out]='\0';
  } else {
    to[0]=' '; to[1]='\0';
  }
}

void cLcd::Split(const char *string, char *string1, char *string2) {

  strncpy(string1,string,wid+1);
  if ( strlen(string) >wid ) {
    strncpy(string2,string+wid,wid+1);
  } else {
    string2[0]=' '; string2[1]='\0';
  }
}

void cLcd::Write(int line, const char *string) { // used for any text output to LCDd

  char workstring[256];
  unsigned int i,out;
  if (hgt > 2) {
    sprintf(workstring,"widget_set VDR line%d 1 %d \"",line,line);
  } else if (LineMode==0) {
    sprintf(workstring,"widget_set VDR line%d %d %d \"",line,(line==2||line==4)?wid+1:1,(line<3)?1:2 );	  
  } else {
    sprintf(workstring,"widget_set VDR line%d %d %d \"",line,(line==3||line==4)?wid+1:1,(line==1||line==4)?1:2);
  }	  
  out=strlen(workstring);
  for (i=0;(i<strlen(string)) && (i<wid);i++)
    workstring[out++] = LcdTransTbl[ (unsigned char) string[i] ]; // char mapping see lcdtranstbl.h
  workstring[out++] =  '"'; workstring[out++] =  '\n';  workstring[out]   = '\0';
  sock_send_string(sock,workstring);
}

#define FullCycle 10  // seconds
#define TimeCycle 7   // seconds 

void cLcd::GetTimeDateStat( char *string, unsigned int OutStateData[] ) {
  time_t t;
  struct tm *now;
  unsigned int i;
  const char States[] = ".-*>"; // '.' ... not present, '-' ... present, '*' ... recording, '>' ... replaying
  bool ShowStates=false;
  unsigned int offset;
  
  offset=(wid>36)?20:0;
  for (i=0; i<LCDMAXCARDS && (!ShowStates) ; i++) ShowStates = ShowStates || (OutStateData[i]>1);

  t = time(NULL);
  now = localtime(&t);

  if ( offset || !( ShowStates && ((t%FullCycle) >= TimeCycle) )) {  
    if (wid > 19) 
      snprintf(string,wid+1,"<%s %02d.%02d %02d:%02d:%02d>",
        WeekDayName(now->tm_wday), now->tm_mday, now->tm_mon+1, now->tm_hour, now->tm_min,now->tm_sec);
    else
      snprintf(string,wid+1,"<%02d.%02d %02d:%02d:%02d>",
        now->tm_mday, now->tm_mon+1, now->tm_hour, now->tm_min,now->tm_sec);
  }

  if ( offset || ( ShowStates && ((t%FullCycle) >= TimeCycle) )) {
    for (i=0; i<LCDMAXCARDS; i++) {
      snprintf(string+offset,5," %d:%c", i,States[ OutStateData[i] ] );
      offset+=4;
    }
  }

}

#define WakeUpCycle 125000 // us 
#define VolumeKeep  1500000 // us

void cLcd::Action(void) { // LCD output thread
  unsigned int i,j, barx=1, bary=1, barl=0, ScrollState=0, ScrollLine=1; 
  int Current=0, Total=1, scrollpos=0,scrollspeed=3,scrollcnt=0, scrollwait=10, scrollwaitcnt=10;
  struct timeval now, voltime;
  char workstring[256];
  cLcd::ThreadStates PrevState=Menu;
  struct cLcd::StateData OutStateData;
  bool Lcddirty[LCDMAXSTATES][4];
  bool LcdShiftkeyPressed=false;
  
  syslog(LOG_INFO, "LCD output thread started (pid=%d), display size: %dx%d", getpid(),hgt,wid);
  cLcd::Write(1," Welcome  to  V D R\0"); 
  cLcd::Write(2,"--------------------\0"); 
  cLcd::Write(3,"Video Disk Recorder\0"); 
  cLcd::Write(4,"Version: "VDRVERSION"\0"); 
  sleep(5); bool volume=false; OutStateData.showvolume=false; ThreadStateData.showvolume=false;
  
  if (primaryDvbApi) for (int k=0; k<primaryDvbApi->NumDevices() ; k++) SetCardStat(k,1); 
  
  voltime.tv_sec=0;
  for (i=0;i<LCDMAXSTATES;i++) for (j=0;j<4;j++) Lcddirty[i][j]=true;
  time_t nextLcdUpdate = (time(NULL)/60)*60+60;

  while (true) { // main loop, wakes up every WakeUpCycle, any output to LCDd is done here  
    gettimeofday(&now,NULL);

    //  epg update
 
    if ( time(NULL) > nextLcdUpdate ) { 
      const cEventInfo *Present = NULL;
      cMutexLock MutexLock;
      const cSchedules *Schedules = cSIProcessor::Schedules(MutexLock);
      if (Schedules) { 
         const cSchedule *Schedule = Schedules->GetSchedule();
         if (Schedule) { 
            const char *PresentTitle, *PresentSubtitle;
            PresentTitle = NULL; PresentSubtitle = NULL;
            if ((Present = Schedule->GetPresentEvent()) != NULL) {
               nextLcdUpdate=Present->GetTime()+Present->GetDuration();
               PresentTitle = Present->GetTitle();
               PresentSubtitle = Present->GetSubtitle();
               if ( (!isempty(PresentTitle)) && (!isempty(PresentSubtitle)) )
                  SetRunning(false,Present->GetTimeString(),PresentTitle,PresentSubtitle);
                  else if (!isempty(PresentTitle)) SetRunning(false,Present->GetTimeString(),PresentTitle);
            } else 
               SetRunning(false,"No EPG info available.\0", NULL);  // XXX tr !!!
            if ((Present = Schedule->GetFollowingEvent()) != NULL)
              nextLcdUpdate=(Present->GetTime()<nextLcdUpdate)?Present->GetTime():nextLcdUpdate;
         }
      }
     if ( nextLcdUpdate <= time(NULL) )
         nextLcdUpdate=(time(NULL)/60)*60+60;
      else if ( nextLcdUpdate > time(NULL)+60 )
         nextLcdUpdate=(time(NULL)/60)*60+60;
    }  

    // replaying
    
    if ( (now.tv_usec < WakeUpCycle) && (replayDvbApi) ) {
      char tempbuffer[16];
      replayDvbApi->GetIndex(Current, Total, false);
      sprintf(tempbuffer,IndexToHMSF(Total));
      SetProgress(IndexToHMSF(Current),tempbuffer, (100 * Current) / Total);
    } 

    
    
    // copy 
    
    BeginMutualExclusion();  // all data needed for output are copied here  
      memcpy(&OutStateData,&ThreadStateData, sizeof (cLcd::StateData));
      ThreadStateData.showvolume=false;
      if (ThreadStateData.newscroll) { scrollpos=0; scrollwaitcnt=scrollwait; ThreadStateData.newscroll=false; } 
      for (i=0;i<LCDMAXSTATES;i++) for (j=0;j<4;j++) { 
	ThreadStateData.lcddirty[i][j]=false;
        Lcddirty[i][j]= Lcddirty[i][j] || OutStateData.lcddirty[i][j];	
      }	
    EndMutualExclusion();

    // scroller
    
    if ( (OutStateData.State==PrevState) && ( PrevState == Replay || PrevState == Menu || PrevState == Title ) ) {
      switch (PrevState) {
	case Replay:
	  ScrollState=LCDREPLAY; ScrollLine=1;	
	break;  	
	case Menu:
	  ScrollState=LCDMENU;   ScrollLine=1;	
	break;  	
	case Title:
	  ScrollState=LCDTITLE;  ScrollLine=2;	
	break;  	
        default:  
        break; 
      }
      
      if ( ( strlen(OutStateData.lcdfullbuffer[ScrollState]) > (2*wid+3) ) 
  	    && !(scrollcnt=(scrollcnt+1)%scrollspeed) ) {
	if ( (scrollpos) || (scrollwaitcnt-- < 1) ) {      
          scrollpos=(scrollpos+1)%strlen(OutStateData.lcdfullbuffer[ScrollState]);
          if  ( scrollpos==1 ) scrollwaitcnt=scrollwait;
          for (i=0; i<wid; i++) {
	    OutStateData.lcdbuffer[ScrollState][ScrollLine][i]=
              OutStateData.lcdfullbuffer[ScrollState][(scrollpos+i)%strlen(OutStateData.lcdfullbuffer[ScrollState])];      
          } 
          OutStateData.lcdbuffer[ScrollState][ScrollLine][wid]='\0';
          for (i=0; i<wid; i++) {
	    OutStateData.lcdbuffer[ScrollState][ScrollLine+1][i]=
	      OutStateData.lcdfullbuffer[ScrollState][(scrollpos+wid+i)%strlen(OutStateData.lcdfullbuffer[ScrollState])];      
          } 
          OutStateData.lcdbuffer[ScrollState][ScrollLine+1][wid]='\0';
          Lcddirty[ScrollState][ScrollLine]=Lcddirty[ScrollState][ScrollLine+1]=true; 
	}
      }	    
    }

    // volume  
   
    if (OutStateData.showvolume) gettimeofday(&voltime,NULL);
    if ( voltime.tv_sec != 0) { // volume
      if (  ((now.tv_sec - voltime.tv_sec)*1000000+now.tv_usec-voltime.tv_usec ) > 1500000  ) {
	voltime.tv_sec=0;
        OutStateData.barx=1; OutStateData.bary=1; OutStateData.barl=0; volume=false;
      } else {
	volume=true;      
	OutStateData.barx=1; OutStateData.bary=((hgt==2)?2:3);
	OutStateData.barl=(cellwid*((hgt==2)?2:1)*wid*OutStateData.volume)/255;      
      }	      
    }	    
    if (volume) OutStateData.State = Vol;
    // modes
    
    switch (OutStateData.State) {

      case Menu: // display menu = 0
        LineMode=1; if (!volume) { OutStateData.barx=1; OutStateData.bary=1; OutStateData.barl=0; }
        if (PrevState != Menu) for (i=0;i<4;i++) Lcddirty[LCDMENU][i]=true;
        for (i=0;i<4;i++) if (Lcddirty[LCDMENU][i]) {
          cLcd::Write(i+1,OutStateData.lcdbuffer[LCDMENU][i]);
          Lcddirty[LCDMENU][i]=false;
        }        
        PrevState=Menu; 
      break;

      case Title: // Display 'titlescsreen' = 1
        LineMode=0;
        if ( (now.tv_usec < WakeUpCycle) || (PrevState != Title) ) { 
          cLcd::GetTimeDateStat(workstring,OutStateData.CardStat);
          cLcd::Write(1,workstring);
        } 
        if (PrevState != Title) for (i=1;i<4;i++) Lcddirty[LCDTITLE][i]=true;
        for (i=1;i<4;i++) if (Lcddirty[LCDTITLE][i]) { 
          cLcd::Write(i+1,OutStateData.lcdbuffer[LCDTITLE][i]); 
          Lcddirty[LCDTITLE][i]=false; 
        }
        PrevState = Title;
      break;

      case Replay: // Display date/time during replaying = 2
        LineMode=1;
        if ( (now.tv_usec < WakeUpCycle) || (PrevState != Replay) ) { 
          cLcd::GetTimeDateStat(workstring,OutStateData.CardStat);
          cLcd::Write(1,workstring);
        } 
        if (PrevState != Replay) { scrollpos=0; for (i=1;i<4;i++) Lcddirty[LCDREPLAY][i]=true; }
        for (i=1;i<4;i++) if (Lcddirty[LCDREPLAY][i]) { 
          cLcd::Write(i+1,OutStateData.lcdbuffer[LCDREPLAY][i]); 
          Lcddirty[LCDREPLAY][i]=false; 
        }
        PrevState = Replay;
      break;

      case Misc: // Display messages  = 3
        LineMode=0;
        if (PrevState != Misc) for (i=0;i<4;i++) Lcddirty[LCDMISC][i]=true;
        for (i=0;i<4;i++) if (Lcddirty[LCDMISC][i]) {
          cLcd::Write(i+1,OutStateData.lcdbuffer[LCDMISC][i]);
          Lcddirty[LCDMISC][i]=false;
        }
        PrevState = Misc;
      break;

      case Vol: // Display Volume = 4
        LineMode=0;
        if (PrevState != Vol) for (i=0;i<4;i++) Lcddirty[LCDVOL][i]=true;
        for (i=0;i<4;i++) if (Lcddirty[LCDVOL][i]) {
          cLcd::Write(i+1,OutStateData.lcdbuffer[LCDVOL][i]);
          Lcddirty[LCDVOL][i]=false;
        }
        PrevState = Vol;
      break;
      
      default: // quite impossible :) 
      break;
    }

    // bargraph
    if ( (OutStateData.barx != barx) || (OutStateData.bary != bary) || (OutStateData.barl != barl) ) {
      sprintf(workstring,"widget_set VDR prbar %d %d %d\n",OutStateData.barx,OutStateData.bary,OutStateData.barl);
      sock_send_string(sock,workstring);
      barx=OutStateData.barx; bary=OutStateData.bary; barl=OutStateData.barl;          
    }

    // keys
    workstring[0]='\0'; sock_recv(sock, workstring, 256);
    if ( strlen(workstring) > 4 ) {
      for (i=0; i < (strlen(workstring)-4); i++ ) {	    
	if (workstring[i]=='k' && workstring[i+1]=='e' && workstring[i+2]=='y' && workstring[i+3]==' ') {
	  if (workstring[i+4] == LcdShiftKey) {
	    LcdShiftkeyPressed = ! LcdShiftkeyPressed;
	    if (LcdShiftkeyPressed)
	       sock_send_string(sock,"screen_set VDR -heartbeat on\n");
	    else
	       sock_send_string(sock,"screen_set VDR -heartbeat off\n");
          }		  
	  if ( (workstring[i+4] != LcdShiftKey) ) {
            if (LcdShiftkeyPressed) {		  
	      syslog(LOG_INFO, "shiftkey  pressed: %c",  workstring[i+4] );
	      LcdShiftkeyPressed=false;
	      sock_send_string(sock,"screen_set VDR -heartbeat off\n");
	    } else {
              syslog(LOG_INFO, "normalkey pressed: %c",  workstring[i+4] );
            }	    
          }		  
	}       	
      }
    }
    
    usleep(WakeUpCycle-(now.tv_usec%WakeUpCycle)); // sync to systemtime for nicer time output 
  }
}
