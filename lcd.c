#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <vdr/config.h>
#include <vdr/tools.h>
#include <vdr/remote.h>
#include "setup.h"
#include "lcd.h"
#include "sockets.h"
#include <vdr/plugin.h>

#ifdef LCD_EXT_KEY_CONF
#include LCD_EXT_KEY_CONF
#else
#include "lcdkeyconf.h"
#endif

// character mapping for output, see cLcd::Write
#include "lcdtranstbl.h"

#define LCDMENU   0
#define LCDTITLE  1
#define LCDREPLAY 2
#define LCDMISC   3
#define LCDVOL    4

// public:

cLcd::cLcd() {
  int i, j;
  connected = false;
  suspended = false;
  memset(&ThreadStateData,0,sizeof(ThreadStateData));
  memset(&LastState,0,sizeof(LastState));
  memset(&StringBuffer,0,sizeof(StringBuffer));
  LastStateP=LineMode=0;
  ToggleMode=false;
  sock = wid = hgt = cellwid = cellhgt = 0;
  closing = false;
  host = NULL;
  replayDvbApi = NULL;
  primaryDvbApi = NULL;

  for (i=0;i<LCDMAXSTATES;i++) for (j=0;j<4;j++) {
    ThreadStateData.lcdbuffer[i][j][0]=' ';
    ThreadStateData.lcdbuffer[i][j][1]='\0';
    ThreadStateData.lcddirty[i][j]=true;
  }
  ThreadStateData.State=Menu;
  for (i=0;i<LCDMAXSTATEBUF;i++) LastState[i]=Title; 
  ThreadStateData.barx=1;
  ThreadStateData.bary=1;
  channelSwitched = false;
  SummaryText = NULL;
  SummaryTextL = SummaryCurrent = port = LCDd_dead = 0;
  LastProgress = (time_t) 0;

#if VDRVERSNUM < 10711
  conv = new cCharSetConv(cCharSetConv::SystemCharacterTable() ? cCharSetConv::SystemCharacterTable() : "UTF-8", "ISO-8859-1");
#else
  conv = new cCharSetConv(NULL, "ISO-8859-1");
#endif
}

cLcd::~cLcd() {
  cLcd::Cancel(2);
  if (connected) { cLcd::Close(); }
  if (conv) { delete(conv); }
  if (host) { free(host); }
}

bool cLcd::Connect( const char *host, unsigned int port ) {
  if (asprintf(&(this->host), "%s", host) < 0) {
	  syslog(LOG_ERR, "lcdproc: error allocating memory in asprintf");
	  return false;
  }
  this->port = port;
  cLcd::Open();

  cLcd::Start(); SetThreadState(Title); // Start thread
  return true;
}

bool cLcd::Open() {

  char istring[1024];
  unsigned int i=0;

  if ( ( sock = sock_connect(host,port) ) < 1) {
    syslog(LOG_INFO, "could not establish connection to LCDd at %s:%d.",host,port);
    return connected=false;
  }

  ToggleMode=false;
  sock_send_string(sock, "hello\n");
  usleep(500000); // wait for a connect message
  memset(istring,0,1024);
  sock_recv(sock, istring, 1024);

  if ( strncmp("connect LCDproc",istring,15) != 0 ) {
    syslog(LOG_INFO, "LCDd at %s:%d does not respond.",host,port);
    cLcd::Close();
    return connected=false;
  }

  while ( ((int)i<((int)strlen(istring)-5)) && (strncmp("lcd",istring+i,3) != 0 ) ) i++;

  if (sscanf(istring+i,"lcd wid %3d hgt %3d cellwid %3d cellhgt %3d", &wid, &hgt, &cellwid, &cellhgt)) connected=true;

  if ((hgt < 4 ) || (wid < 16)) connected = false; // 4 lines are needed, may work with more than 4 though
  if ((hgt==2) && (wid>31)) { connected = true; wid=wid/2; LineMode=0; }   // 2x32-2x40
  else if ( (hgt==2) && (wid>15) && (wid<32) ) { connected = true; ToggleMode=true; }  // 2x16-2x31
  if (!connected) {
    syslog(LOG_INFO, "Minimum Display Size is 2x16. Your LCD is to small, sorry.");
    cLcd::Close();
    return connected;
  }

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
  syslog(LOG_INFO, "connection to LCDd at %s:%d established.",host,port);
  return connected;
}

void cLcd::Close() {
  char istring[1024];
  fprintf(stderr,"Close Called \n");
  if (connected) {
   closing = true;
   sock_send_string(sock,"screen_del VDR\n");
   sock_recv(sock, istring, 1024);
   usleep(1000000);
   sock_close(sock);
   usleep(500000);
  }else{
   fprintf(stderr,"Not Connected !!! \n");
  }
  connected=false; sock=wid=hgt=cellwid=cellhgt=0;
}

bool cLcd::Suspend() {
  fprintf(stderr,"Suspend Called \n");
  if (connected) {
   suspended = true;
   connected=false;
   sock_send_string(sock,"screen_set VDR -priority hidden\n");
   return true;
  }else{
   fprintf(stderr,"Not Connected !!! \n");
   return false;
  }
}

bool cLcd::Resume() {
  fprintf(stderr,"Resume Called \n");
  if (suspended) {
   connected=true;
   suspended = false;
   sock_send_string(sock,"screen_set VDR -priority info\n");
   return true;
  }else{
   fprintf(stderr,"Not suspended !!! \n");
   return false;
  }
}

const char* cLcd::Convert(const char *s) {
	// do character recoding to ISO-8859-1
	if (!s) return s;
	if (strlen(s)==0) return s;
	const char *s_converted = conv->Convert(s);
	if (s_converted == s) {
	  esyslog("lcdproc-plugin: conversion from %s to ISO-8859-1 failed.", cCharSetConv::SystemCharacterTable());
	  esyslog("lcdproc-plugin: '%s'",s);
	}
	return s_converted;
}

void cLcd::Info() { // just for testing ...
 if (connected)
   printf("sock %d, wid %d, hgt %d, cellwid %d, cellhgt %d\n",sock, wid, hgt, cellwid, cellhgt);
 else
   printf("not connected\n");
}

void cLcd::SetTitle( const char *string) {
  if (!connected) return;

  const char* c_string = Convert(string);

  char title[wid+1];

  // we need to know the length of the translated string "Schedule"
  const char *trstring=trVDR("Schedule - %s");
  int trstringlength=strlen(trstring)-3;

  if (strncmp(trstring,c_string,trstringlength)==0) {
    // it is the title of the "Schedule" menu, so we replace "Schedule" with ">"
    title[0]='>'; snprintf( title+1, wid,"%s",c_string+trstringlength);
  } else if (strlen(c_string) > (wid-1)) {
    snprintf( title, wid+1,"%s",c_string);
  } else {
    memset(title,' ',wid/2+1);
    snprintf(title + ( (wid-strlen(c_string))/2 ),wid-( (wid-strlen(c_string))/2 ),"%s",c_string); // center title
    for (unsigned int i=0;i<strlen(title);i++) title[i]=toupper(title[i]); // toupper
  }
  cLcd::SetLine(LCDMENU,0,title);
}

void cLcd::SetMain( unsigned int n, const char *string, bool isConverted) {
	const char* c_string;

	if (!connected) return;

	if (isConverted)
		c_string = string;
	else
		c_string = Convert(string);

	char line2[wid+1];
	char line3[wid+1];

	if (c_string != NULL) {
		BeginMutualExclusion();
		cLcd::Copy(ThreadStateData.lcdfullbuffer[n],c_string,LCDMAXFULLSTRING-3);
		unsigned int i = strlen(ThreadStateData.lcdfullbuffer[n]);
		if (i > (((ToggleMode)?1:2)*wid)) {
		ThreadStateData.lcdfullbuffer[n][i++]=' ';
		ThreadStateData.lcdfullbuffer[n][i++]='*';
		ThreadStateData.lcdfullbuffer[n][i++]=' ';
		}
		ThreadStateData.lcdfullbuffer[n][i]='\0';
		ThreadStateData.newscroll=true;
		cLcd::Copy(StringBuffer,c_string,2*wid);
		cLcd::Split(StringBuffer,line2,line3);
		EndMutualExclusion();
		cLcd::SetBuffer(n,NULL,line2,line3,NULL);
	} else {
		BeginMutualExclusion();
		//cLcd::SetBuffer(n,NULL," \0"," \0",NULL);
		ThreadStateData.lcdfullbuffer[n][0]='\0';
		EndMutualExclusion();
	}
}

void cLcd::SetHelp( unsigned int n, const char *Red, const char *Green, const char *Yellow, const char *Blue) {
  if (!connected) return;

  char help[2*wid], red[wid+1], green[wid+1], yellow[wid+1], blue[wid+1];
  unsigned int allchars=0, i,j , empty=0, spacewid=1;
  char *longest, *longest1, *longest2;

  const char* c_Red    = Convert(Red);
  if ( c_Red==NULL || c_Red[0]=='\0' ) {
    empty++; red[0]=' '; red[1]='\0';
  } else {
    j=i=0; while ( (i<wid) && (c_Red[i] != '\0') ) {
      if (c_Red[i] !=' ') {red[j]=c_Red[i]; j++; }
      i++;
    } red[j]='\0';
    allchars+=strlen(red);
  }
  const char* c_Green  = Convert(Green);
  if ( c_Green==NULL || c_Green[0]=='\0' )  {
    empty++; green[0]=' '; green[1]='\0';
  } else {
    j=i=0; while ( (i<wid) && (c_Green[i] != '\0') ) {
      if (c_Green[i] !=' ') {green[j]=c_Green[i]; j++; }
      i++;
    } green[j]='\0';
    allchars+=strlen(green);
  }
  const char* c_Yellow = Convert(Yellow);
  if ( c_Yellow==NULL || c_Yellow[0]=='\0' ) {
    empty++; yellow[0]=' '; yellow[1]='\0';
  } else {
    j=i=0; while ( (i<wid) && (c_Yellow[i] != '\0') ) {
      if (c_Yellow[i] !=' ') {yellow[j]=c_Yellow[i]; j++; }
      i++;
    } yellow[j]='\0';
    allchars+=strlen(yellow);
  }
  const char* c_Blue   = Convert(Blue);
  if ( c_Blue==NULL || c_Blue[0]=='\0' ) {
    empty++; blue[0]=' '; blue[1]='\0';
  } else {
    j=i=0; while ( (i<wid) && (c_Blue[i] != '\0') ) {
      if (c_Blue[i] !=' ') {blue[j]=c_Blue[i]; j++; }
      i++;
    } blue[j]='\0';
    allchars+=strlen(blue);
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

  const char* c_string = Convert(string);

  char statstring[2*wid+1];

  if (c_string == NULL) {
    cLcd::SetMain(LCDMENU,StringBuffer,true);
  } else {
    cLcd::Copy(statstring,c_string,2*wid);
    cLcd::SetMain(LCDMENU,statstring);
  }
}

void cLcd::SetWarning( const char *string) {
  if (!connected) return;

  const char* c_string = Convert(string);

  char statstring[2*wid+1];

  if (c_string != NULL) {
    cLcd::Copy(statstring,c_string,2*wid);
    cLcd::Clear(LCDMISC);
    cLcd::SetMain(LCDMISC,statstring,true);
    cLcd::SetThreadState(Misc);
  }
}

void cLcd::ShowVolume(int vol, bool absolute ) {
  BeginMutualExclusion();
    if (absolute)
        ThreadStateData.volume=vol;
      else
        ThreadStateData.volume+=vol;
    ThreadStateData.muted=(ThreadStateData.volume==0);
    ThreadStateData.showvolume=true;
  EndMutualExclusion();
  if (!connected) return;
  if (ThreadStateData.muted) {
    cLcd::SetLine(Vol,0," ");
    cLcd::SetLine(Vol,1,Convert(tr("Mute")));
    cLcd::SetLine(Vol,2," ");
    cLcd::SetLine(Vol,3," ");
  } else {
    if (hgt==2) {
      if (ToggleMode) {
	    cLcd::SetLine(Vol,0,Convert(tr("Volume ")));
        cLcd::SetLine(Vol,1," ");
      } else {
        cLcd::SetLine(Vol,0,Convert(tr("Volume ")));
        cLcd::SetLine(Vol,3," ");
        cLcd::SetLine(Vol,1," ");
        cLcd::SetLine(Vol,2," ");
      }
    } else {
      cLcd::SetLine(Vol,0,Convert(tr("Volume ")));
      cLcd::SetLine(Vol,3,"|---|---|---|---|---|---|---|---|---|---");
      cLcd::SetLine(Vol,1,"|---|---|---|---|---|---|---|---|---|---");
      cLcd::SetLine(Vol,2," ");
    }

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
      cLcd::SetLine(LCDREPLAY,(ToggleMode)?0:3,workstring);
      BeginMutualExclusion();
        ThreadStateData.barx=beginw+1+((hgt==2 && !ToggleMode)?wid:0);
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

  const char* c_string = Convert(string);

  BeginMutualExclusion();
    if (c_string != NULL) cLcd::Copy(ThreadStateData.lcdbuffer[n][l],c_string,wid);
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

  char line[1024];
  char line1[1024];

  static char now1[LCDMAXWID+1];
  static char now2[LCDMAXWID+1];

  char *c_string1 = string1 ? strdup(Convert(string1)) : NULL;
  char *c_string2 = string2 ? strdup(Convert(string2)) : NULL;
  char *c_string3 = string3 ? strdup(Convert(string3)) : NULL;

   snprintf(line,1024,"%s%s%s%s%s",
    (string1==NULL || string1[0]=='\0')?"":c_string1,
    ((!(string1==NULL || string1[0]=='\0')) && (!(string2==NULL || string2[0]=='\0')))?" ":"",
    (string2==NULL || string2[0]=='\0')?"":c_string2,
    (string3==NULL || string3[0]=='\0')?"":"|",
    (string3==NULL || string3[0]=='\0')?"":c_string3);
  cLcd::Copy(line1,line,2*wid);

  free(c_string1);
  free(c_string2);
  free(c_string3);


  if (nownext) {
    BeginMutualExclusion();
      cLcd::Split(line1,ThreadStateData.lcdbuffer[LCDMISC][2],ThreadStateData.lcdbuffer[LCDMISC][3]);
      ThreadStateData.lcddirty[LCDMISC][2]=true; ThreadStateData.lcddirty[LCDMISC][3]=true;
    EndMutualExclusion();
    cLcd::SetBuffer(LCDMISC,now1,now2,NULL,NULL);
  } else {
    BeginMutualExclusion();
      cLcd::Split(line1,ThreadStateData.lcdbuffer[LCDTITLE][2],ThreadStateData.lcdbuffer[LCDTITLE][3]);
      ThreadStateData.lcddirty[LCDTITLE][2]=true; ThreadStateData.lcddirty[LCDTITLE][3]=true;
      cLcd::Copy(ThreadStateData.lcdfullbuffer[LCDTITLE],line,LCDMAXFULLSTRING-3);
      unsigned int i = strlen(ThreadStateData.lcdfullbuffer[LCDTITLE]);
      if (i>(((ToggleMode)?1:2)*wid)) {
      ThreadStateData.lcdfullbuffer[LCDTITLE][i++]=' ';
      ThreadStateData.lcdfullbuffer[LCDTITLE][i++]='*';
      ThreadStateData.lcdfullbuffer[LCDTITLE][i++]=' ';
      }
      ThreadStateData.lcdfullbuffer[LCDTITLE][i]='\0';
      snprintf(now1,LCDMAXWID,"%s",ThreadStateData.lcdbuffer[LCDTITLE][2]);
      snprintf(now2,LCDMAXWID,"%s",ThreadStateData.lcdbuffer[LCDTITLE][3]);
    EndMutualExclusion();

  }
}

void cLcd::SummaryInit(const char *string) {

  if (SummaryText)
	  free(SummaryText);
  SummaryText  = strdup(Convert(string));
  SummaryTextL = strlen(SummaryText);
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
      cLcd::SetLine(LCDMISC,i,workstring);
      offset+=wid;
    } else cLcd::SetLine(LCDMISC,i," ");
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
  CriticalArea.Unlock();
}

void cLcd::Copy(char *to, const char *from, unsigned int max) { // eliminates tabs, double blanks, ...

  if (from != NULL) {
    unsigned int i=0 , out=0;
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

void cLcd::Split(char *string, char *string1, char *string2) {

  unsigned int  i,j,ofs;

  if ( hgt>2 && ( strlen(string) < 2*wid) && isdigit(string[0]) && isdigit(string[1]) // beautification ..
       && string[2]==':' && isdigit(string[3]) && isdigit(string[4]) ) {
    char *tmpptr=strpbrk(string,"|");
    if ( ( tmpptr != NULL ) && (ofs=tmpptr-string)<wid && wid-(ofs+1)<2*wid-(j=strlen(string))  ) {
	 ofs=wid-(ofs+1);
	 string[j+ofs]='\0';
	 for (i=j+ofs-1; i>=wid-ofs;i--) string[i]=string[i-ofs];
	 for (i=wid-ofs-1;i<wid;i++) string[i]=' ';
    }
  }

  if ( hgt>2 && ( strlen(string) < 2*wid) && ( strlen(string) >= wid ) ) {  // beautification ..

    if (isalpha(string[wid-1]) && isalpha(string[wid])  ) {
      j=strlen(string);
      for (i=wid-1; (i>0) && (string[i]!=' ') && (string[i]!='|') && (string[i]!=',') && (string[i]!=')')
		          && (string[i]!='-') && (string[i]!='.') && (string[i]!=':') ; i-- ) {}

      if ( ( (2*wid-j) >= (ofs=wid-(i+1)) ) && ofs+j <= 2*wid   ) {
	string[j+ofs]='\0';
	unsigned int k;
	for (k=j+ofs-1;k>i+ofs; k-- ) string[k]=string[k-ofs];
        for (k=0;k<ofs;k++) string[i+k+1]=' ';
      }

    }

    if ( (j=strlen(string)) < 2*wid && isdigit(string[0]) && isdigit(string[1]) &&
		     string[2]==':' && isdigit(string[3]) && isdigit(string[4])   ) {
      ofs=2*wid-strlen(string); ofs=(ofs>6)?6:ofs;
      if (string[wid]=='|') string[wid]=' ';
      if (ofs==6 && string[wid]==' ') ofs--;
      string[j+ofs]='\0';
      for (i=j+ofs-1; i>=wid+ofs;i--) string[i]=string[i-ofs];
      for (i=wid;i<wid+ofs;i++) string[i]=' ';
    }
  }


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

  if (ToggleMode && (line > 2)) return;

  if (ToggleMode || hgt>2 ) {
    sprintf(workstring,"widget_set VDR line%d 1 %d \"",line,line);
  } else if (LineMode==0) {
    sprintf(workstring,"widget_set VDR line%d %d %d \"",line,(line==2||line==4)?wid+1:1,(line<3)?1:2 );
  } else {
    sprintf(workstring,"widget_set VDR line%d %d %d \"",line,(line==3||line==4)?wid+1:1,(line==1||line==4)?1:2);
  }
  // do lcdtranstbl mapping
  out=strlen(workstring);
  for (i=0;(i<strlen(string)) && (i<wid);i++)
    workstring[out++] = LcdTransTbl[LcdSetup.Charmap][ (unsigned char) string[i] ]; // char mapping see lcdtranstbl.h
  workstring[out++] =  '"'; workstring[out++] =  '\n';  workstring[out]   = '\0';
  sock_send_string(sock,workstring);
}

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

  if ( offset || !( ShowStates && ((t%LcdSetup.FullCycle) >= LcdSetup.TimeCycle) )) {
    if (ToggleMode) {
        strcat(string,"                          ");
        snprintf(string+wid-6,8," %02d%s%02d", now->tm_hour, (now->tm_sec%2)?" ":":", now->tm_min);
    }
    else {
    if (wid > 19)
      snprintf(string,wid+1,"<%s %02d.%02d %02d:%02d:%02d>",
        Convert(*WeekDayName(now->tm_wday)), now->tm_mday, now->tm_mon+1, now->tm_hour, now->tm_min,now->tm_sec);
    else
      snprintf(string,wid+1,"<%02d.%02d %02d:%02d:%02d>",
        now->tm_mday, now->tm_mon+1, now->tm_hour, now->tm_min,now->tm_sec);
    }
  }

  if ( offset || ( ShowStates && ((t%LcdSetup.FullCycle) >= LcdSetup.TimeCycle) )) {
    if (LcdSetup.RecordingStatus == 0) {
      for (i=0; i<LCDMAXCARDS; i++) {
       snprintf(string+offset,5," %d:%c", i,States[ OutStateData[i] ] );
       offset+=4;
      }
    }
    else {
      if (ToggleMode || wid < 19) {
          snprintf(string,wid+1,"                                        ");
          snprintf(string+wid-16,wid+1,"%s  %02d%s%02d", Convert(tr("RECORDING")), now->tm_hour, (now->tm_sec%2)?" ":":", now->tm_min);
      }
      else {
      snprintf(string,wid+1,"<%s %02d:%02d:%02d>", Convert(tr("RECORDING")), now->tm_hour, now->tm_min,now->tm_sec);
      }
    }
  }

}

#define WakeUpCycle 125000 // us
#define WorkString_Length 1024

void cLcd::Action(void) { // LCD output thread
  unsigned int i,j, barx, bary, barl, ScrollState, ScrollLine, lasttitlelen;
  int Current, Total, scrollpos, scrollcnt, scrollwaitcnt, lastAltShift, lastBackLight,lastPrio, keycnt;
  struct timeval now, voltime;
  char workstring[WorkString_Length]="";
  char workstring2[101]="";
  char lastkeypressed='\0';
  cLcd::ThreadStates PrevState = Menu;
  struct cLcd::StateData OutStateData;
  bool Lcddirty[LCDMAXSTATES][4];
  bool LcdShiftkeyPressed=false;
  char priostring[35];
  // RT
  static int rtcycle;

  // LCR
  static int lcrCycle;

  memset(&OutStateData,0,sizeof(OutStateData));
  memset(&Lcddirty,0,sizeof(Lcddirty));

  time_t nextLcdUpdate, lastUserActivity;

  while (true) {  // outer (reconnect) loop
	  barx=1; bary=1; barl=0; ScrollState=0; ScrollLine=1; lasttitlelen=0;
	  Current=0, Total=1, scrollpos=0, scrollcnt=0, scrollwaitcnt=10, lastAltShift=0, lastBackLight=0 ,lastPrio=0, keycnt=0;
	  lastkeypressed='\0';
	  PrevState=Menu;
	  OutStateData.State=Title;
	  LcdShiftkeyPressed=false;
	  LCDd_dead=0;

	  while (! connected) {
		  sleep(120);
		  Open();
	  }

	  // backlight init
	  if ((lastBackLight=LcdSetup.BackLight))
		  sock_send_string(sock,"backlight on\n");
	  else
		  sock_send_string(sock,"backlight off\n");

	  // prio init
	  if (LcdSetup.SetPrio) {
		  if (LcdSetup.SetPrio == 1){
			  sprintf(priostring,"screen_set VDR -priority %d\n", LcdSetup.ClientPrioN );
			  lastPrio=LcdSetup.ClientPrioN;
		  } else {
			  sprintf(priostring,"screen_set VDR -priority %d\n", LcdSetup.ClientPrioH );
			  lastPrio=LcdSetup.ClientPrioH;
		  }

		  sock_send_string(sock,priostring);
	  }
	  lastUserActivity=time(NULL);

	  syslog(LOG_INFO, "LCD output thread started (pid=%d), display size: %dx%d", getpid(),hgt,wid);
	  cLcd::Write(LcdSetup.ShowTime?1:4," Welcome  to  V D R\0");
	  cLcd::Write(LcdSetup.ShowTime?2:3,"--------------------\0");
	  cLcd::Write(LcdSetup.ShowTime?3:1,"Video Disk Recorder\0");
	  cLcd::Write(LcdSetup.ShowTime?4:2,"Version: " VDRVERSION "\0");

	  // Output init
	  if (LcdSetup.OutputNumber > 0){
		  char lcdCommand[100];

		  for (int o=0; o <  LcdSetup.OutputNumber; o++){
			  sprintf(lcdCommand,"output %d\n",1 << o);
			  sock_send_string(sock,lcdCommand);
			  usleep(150000);
		  }
		  for (int o= LcdSetup.OutputNumber-1; o >= 0; o--){
			  sprintf(lcdCommand,"output %d\n",1 << o);
			  sock_send_string(sock,lcdCommand);
			  usleep(150000);
		  }
		  sprintf(lcdCommand,"output on\n");
		  sock_send_string(sock,lcdCommand);
		  usleep(500000);
		  sprintf(lcdCommand,"output off\n");
		  sock_send_string(sock,lcdCommand);
	  }


	  sleep(3); bool volume=false; OutStateData.showvolume=false; ThreadStateData.showvolume=false;

	  if (primaryDvbApi) for (int k=0; k<primaryDvbApi->NumDevices() ; k++) SetCardStat(k,1);

	  voltime.tv_sec=0;
	  for (i=0;i<LCDMAXSTATES;i++) for (j=0;j<4;j++) Lcddirty[i][j]=true;
	  nextLcdUpdate = 0; // trigger first update immediately
	  while (true) { // main loop, wakes up every WakeUpCycle, any output to LCDd is done here
		  gettimeofday(&now,NULL);

		  //  epg update
		  if (channelSwitched) {
			  channelSwitched = false;
			  lastUserActivity=time(NULL);
			  nextLcdUpdate = 0; //trigger next epg update
		  }

		  if ( time(NULL) > nextLcdUpdate ) {
			  cChannel *channel = Channels.GetByNumber(primaryDvbApi->CurrentChannel());
			  const cEvent *Present = NULL;
			  cSchedulesLock schedulesLock;
			  const cSchedules *Schedules = cSchedules::Schedules(schedulesLock);
			  if (Schedules) {
				  const cSchedule *Schedule = Schedules->GetSchedule(channel->GetChannelID());
				  if (Schedule) {
					  const char *PresentTitle, *PresentSubtitle;
					  PresentTitle = NULL; PresentSubtitle = NULL;
					  if ((Present = Schedule->GetPresentEvent()) != NULL) {
						  nextLcdUpdate=Present->StartTime()+Present->Duration();
						  PresentTitle = Present->Title();
						  PresentSubtitle = Present->ShortText();
						  if ( (LcdSetup.ShowSubtitle) &&  (!isempty(PresentTitle)) && (!isempty(PresentSubtitle)) )
							  SetRunning(false,Present->GetTimeString(),PresentTitle,PresentSubtitle);
						  else if ( (LcdSetup.ShowSubtitle) && !isempty(PresentTitle)) SetRunning(false,Present->GetTimeString(),PresentTitle);
					  } else
						  SetRunning(false,tr("No EPG info available."), NULL);
					  if ((Present = Schedule->GetFollowingEvent()) != NULL)
						  nextLcdUpdate=(Present->StartTime()<nextLcdUpdate)?Present->StartTime():nextLcdUpdate;
						  rtcycle = 10; // RT
						  lcrCycle = 10; // LCR
				  }
			  }
			  if ( nextLcdUpdate <= time(NULL) )
				  nextLcdUpdate=(time(NULL)/60)*60+60;
		  }

		  // get&display Radiotext
		  if (++rtcycle > 10) {	// every 10 times
			  cPlugin *p;
			  p = cPluginManager::CallFirstService("RadioTextService-v1.0", NULL);
			  if (p) {
				  RadioTextService_v1_0 rtext;
				  if (cPluginManager::CallFirstService("RadioTextService-v1.0", &rtext)) {
					  if (rtext.rds_info == 2 && strstr(rtext.rds_title, "---") == NULL) {
						  char timestr[20];
						  sprintf(timestr, "%02d:%02d", rtext.title_start->tm_hour, rtext.title_start->tm_min);
						  SetRunning(false, timestr, rtext.rds_title, rtext.rds_artist);
					  }
					  else if (rtext.rds_info > 0) {
						  SetRunning(false, NULL, rtext.rds_text);
					  }
				  }
			  }
			  rtcycle = 0;
			  //printf("lcdproc - get Radiotext ...\n");
		  }
		  // get&display LcrData
		  if (lcrCycle++ == 10) // every 10 times
		  {
			  lcrCycle = 0;
			  cPlugin *p;
			  p = cPluginManager::CallFirstService("LcrService-v1.0", NULL);
			  if (p)
			  {
				  LcrService_v1_0 lcrData;
				  if (cPluginManager::CallFirstService("LcrService-v1.0", &lcrData))
				  {
					  if ( strstr( lcrData.destination, "---" ) == NULL )
					  {
						  SetRunning(false, (const char *)lcrData.destination, (const char *)lcrData.price, (const char         *)lcrData.pulse);
						  nextLcdUpdate = 0; //trigger next epg update
					  }
				  }
			  }
		  }

		  // replaying

		  if ( (now.tv_usec < WakeUpCycle) && (replayDvbApi) ) {
			  char tempbuffer[16];
			  replayDvbApi->GetIndex(Current, Total, false);
			  Total= (Total==0) ? 1 : Total;
			  double FramesPerSecond = replayDvbApi->FramesPerSecond();
			  sprintf(tempbuffer, "%s", (const char*)IndexToHMSF(Total, false, FramesPerSecond));
			  SetProgress(IndexToHMSF(Current, false, FramesPerSecond), tempbuffer, (100 * Current) / Total);
		  }



		  // copy

		  BeginMutualExclusion();  // all data needed for output are copied here
		  memcpy(&OutStateData,&ThreadStateData, sizeof (cLcd::StateData));
		  ThreadStateData.showvolume=false;
		  if (ThreadStateData.newscroll) { scrollpos=0; scrollwaitcnt=LcdSetup.Scrollwait; ThreadStateData.newscroll=false; }
		  for (i=0;i<LCDMAXSTATES;i++) for (j=0;j<4;j++) {
			  ThreadStateData.lcddirty[i][j]=false;
			  Lcddirty[i][j]= Lcddirty[i][j] || OutStateData.lcddirty[i][j];
		  }
		  EndMutualExclusion();

		  // scroller

		  if ( (OutStateData.State==PrevState) && ( OutStateData.State == Replay || OutStateData.State == Menu || OutStateData.State == Title ) ) {
			  switch (OutStateData.State) {
			  case Replay:
				  ScrollState=LCDREPLAY; ScrollLine=1;
				  break;
			  case Menu:
				  ScrollState=LCDMENU;   ScrollLine=1;
				  break;
			  case Title:
				  ScrollState=LCDTITLE;
				  if (!ToggleMode) {
					  ScrollLine=2;
				  } else {
					  ScrollLine=1;
					  strncpy(workstring2,OutStateData.lcdbuffer[LCDTITLE][1],wid);
				//	  if (LcdSetup.ShowTime) {
				//	      char tmpbuffer[1024];
				//	      strcpy(tmpbuffer,OutStateData.lcdbuffer[LCDTITLE][1]);
				//	      strcat(tmpbuffer," * ");
				//	      strcat(tmpbuffer, OutStateData.lcdfullbuffer[LCDTITLE]);
				//	      strcpy(OutStateData.lcdfullbuffer[LCDTITLE],tmpbuffer);
				//	  }
					  strncpy(OutStateData.lcdbuffer[LCDTITLE][1],OutStateData.lcdfullbuffer[LCDTITLE],wid);
				  }
				  if ( strlen(OutStateData.lcdfullbuffer[LCDTITLE]) != lasttitlelen ) {
					  lasttitlelen=strlen(OutStateData.lcdfullbuffer[LCDTITLE]);
					  scrollpos=0; scrollwaitcnt=LcdSetup.Scrollwait; ThreadStateData.newscroll=false;
				  }
				  break;
			  default:
				  break;
			  }

			  if ( ( strlen(OutStateData.lcdfullbuffer[ScrollState]) > (((ToggleMode)?1:2)*wid+3) )
					  && ( (scrollpos) || !(scrollwaitcnt=(scrollwaitcnt+1)%LcdSetup.Scrollwait) ) ) {
				  if ( !(scrollcnt=(scrollcnt+1)%LcdSetup.Scrollspeed)  ) {
					  scrollpos=(scrollpos+1)%strlen(OutStateData.lcdfullbuffer[ScrollState]);
					  if  ( scrollpos==1 ) scrollwaitcnt=0;
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
			  } else if ( (!LcdSetup.ShowTime) || (ToggleMode) ) Lcddirty[LCDTITLE][1]=true;
		  }

		  // volume

		  if (OutStateData.showvolume) gettimeofday(&voltime,NULL);
		  if ( voltime.tv_sec != 0) { // volume
			  if (  ((now.tv_sec - voltime.tv_sec)*1000000+now.tv_usec-voltime.tv_usec ) > (100000*LcdSetup.VolumeKeep)  ) {
				  voltime.tv_sec=0;
				  OutStateData.barx=1; OutStateData.bary=1; OutStateData.barl=0; volume=false;
			  } else {
				  volume=true;
				  OutStateData.barx=1; OutStateData.bary=((hgt==2)?2:3);
				  // shortening volume bar in togglemode (lcd with 2x20)
				  OutStateData.barl=(cellwid*((hgt==2 && !ToggleMode)?2:1)*wid*OutStateData.volume)/255;
			  }
		  }
		  if (volume) OutStateData.State = Vol;

		  // modes

		  switch (OutStateData.State) {

		  case Menu: // display menu = 0
			  lastUserActivity=time(NULL);
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
			  if ( (LcdSetup.ShowTime) && ( (now.tv_usec < WakeUpCycle) || (PrevState != Title) || ( (ToggleMode) && (Lcddirty[LCDTITLE][1]) ) ) ) {
			      cLcd::GetTimeDateStat(workstring2,OutStateData.CardStat);
			      cLcd::Write(1,workstring2);
			  }
			  if (PrevState != Title) for (i=LcdSetup.ShowTime?1:0;i<4;i++) Lcddirty[LCDTITLE][i]=true;
			  for (i=LcdSetup.ShowTime?1:0;i<4;i++) if (Lcddirty[LCDTITLE][i]) {
				  cLcd::Write(i+1,OutStateData.lcdbuffer[LCDTITLE][i]);
				  Lcddirty[LCDTITLE][i]=false;
			  }
			  PrevState = Title;
			  break;

		  case Replay: // Display date/time during replaying = 2
			  LineMode=1;
			  if ( !ToggleMode && ((now.tv_usec < WakeUpCycle) || (PrevState != Replay)) ) {
				  cLcd::GetTimeDateStat(workstring,OutStateData.CardStat);
				  cLcd::Write(1,workstring);
			  }
			  if (PrevState != Replay) { scrollpos=0; for (i=1;i<4;i++) Lcddirty[LCDREPLAY][i]=true; }
			  for (i=(ToggleMode)?0:1;i<4;i++) if (Lcddirty[LCDREPLAY][i]) {
				  cLcd::Write(i+1,OutStateData.lcdbuffer[LCDREPLAY][i]);
				  Lcddirty[LCDREPLAY][i]=false;
			  }
			  PrevState = Replay;
			  break;

		  case Misc: // Display messages  = 3
			  LineMode=0;
			  lastUserActivity=time(NULL);
			  if (PrevState != Misc) for (i=0;i<4;i++) Lcddirty[LCDMISC][i]=true;
			  for (i=0;i<4;i++) if (Lcddirty[LCDMISC][i]) {
				  cLcd::Write(i+1,OutStateData.lcdbuffer[LCDMISC][i]);
				  Lcddirty[LCDMISC][i]=false;
			  }
			  PrevState = Misc;
			  break;

		  case Vol: // Display Volume = 4
			  LineMode=0;
			  lastUserActivity=time(NULL);
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

		  // prio
		  //    lastUserActivity=time(NULL);       // <---------------------------------------  UserActivity Dummy !!!
		  if (LcdSetup.SetPrio != 0) {
			  if (LcdSetup.SetPrio == 1){
				  if (lastPrio != LcdSetup.ClientPrioN) {
					  sprintf(priostring,"screen_set VDR -priority %d\n", LcdSetup.ClientPrioN );
					  lastPrio=LcdSetup.ClientPrioN;
					  sock_send_string(sock,priostring);
				  }
			  } else {
				  if(time(NULL) > (lastUserActivity + LcdSetup.PrioWait)) {
					  if (lastPrio != LcdSetup.ClientPrioN) {
						  sprintf(priostring,"screen_set VDR -priority %d\n", LcdSetup.ClientPrioN );
						  lastPrio=LcdSetup.ClientPrioN;
						  sock_send_string(sock,priostring);
					  }
				  }
				  else if (lastPrio != LcdSetup.ClientPrioH) {
					  sprintf(priostring,"screen_set VDR -priority %d\n", LcdSetup.ClientPrioH );
					  lastPrio=LcdSetup.ClientPrioH;
					  sock_send_string(sock,priostring);
				  }
			  }
		  }

		  // backlight
		  if (LcdSetup.BackLight == 2) { // auto
			  if(time(NULL) > (lastUserActivity + LcdSetup.BackLightWait)){
				  if ( lastBackLight != 0) {
					  sock_send_string(sock,"backlight off\n");
					  lastBackLight = 0;
				  }
			  } else if ( lastBackLight != 1) {
				  sock_send_string(sock,"backlight on\n");
				  lastBackLight = 1;
			  }
		  }
		  else if ( lastBackLight != LcdSetup.BackLight) {

			  lastBackLight=LcdSetup.BackLight;
			  if (lastBackLight)
				  sock_send_string(sock,"backlight on\n");
			  else
				  sock_send_string(sock,"backlight off\n");
		  }

		  // keys

		  if ( LcdMaxKeys && (lastAltShift != LcdSetup.AltShift) ) {
			  lastAltShift=LcdSetup.AltShift;
			  if (lastAltShift)
				  sock_send_string(sock,"screen_set VDR -heartbeat slash\n");
			  else
				  sock_send_string(sock,"screen_set VDR -heartbeat heart\n");
		  }


		  if ( !(keycnt=(keycnt+1)%4) ) lastkeypressed='\0';

		  memset(workstring,0,WorkString_Length);
		  workstring[0]='\0';
		  sock_recv(sock, workstring, WorkString_Length);
		  workstring[WorkString_Length-1]='\0';
		  //if (workstring[0] != '\0') syslog(LOG_INFO, "%s",  workstring);

		  //  reconnect if LCDd died

		  sock_send_string(sock, "hello\n");
		  //syslog(LOG_INFO, "workstring1: %d : %d",strlen(workstring),(strlen(workstring)-17));
		  //syslog(LOG_INFO, "workstring2: %s",workstring);

		  j=0;
		  for (i=0; (int)i < ((int)strlen(workstring)-17); i++ ) {
			  if(strncmp("connect LCDproc",workstring+i,15) == 0){
				  j=1;
				  break;
			  }
		  }
		  if(!suspended) {
			  if (j==0){
				  if(LCDd_dead > MAX_LCDd_dead) {
					  syslog(LOG_INFO, "Connection to LCDd at %s:%d lost, trying to reestablish.",host,port);
					  Close();
					  break;
				  }
				  LCDd_dead++;
			  } else LCDd_dead=0;
		  }


		  // keys (again)

		  if ( LcdMaxKeys && ( strlen(workstring) > 4 ) ) {
			  for (i=0; i < (strlen(workstring)-4); i++ ) {
				  if (workstring[i]=='k' && workstring[i+1]=='e' && workstring[i+2]=='y'
					  && workstring[i+3]==' ' && workstring[i+4]!=lastkeypressed ) {
					  lastkeypressed=workstring[i+4];
					  for (j=0; j<LcdMaxKeys && workstring[i+4]!=LcdUsedKeys[j]; j++ ) {}
					  if (workstring[i+4] == LcdShiftKey) {
						  LcdShiftkeyPressed = ! LcdShiftkeyPressed;
						  if (LcdShiftkeyPressed)
							  sock_send_string(sock,"screen_set VDR -heartbeat on\n");
						  else
							  sock_send_string(sock,"screen_set VDR -heartbeat off\n");
					  }
					  if ( (workstring[i+4] != LcdShiftKey) ) {
						  if (LcdShiftkeyPressed) {
							  //syslog(LOG_INFO, "shiftkey  pressed: %c %d",  workstring[i+4],j);
							  cRemote::Put(LcdShiftMap[j]);
							  LcdShiftkeyPressed=false;
							  sock_send_string(sock,"screen_set VDR -heartbeat off\n");
						  } else {
							  //syslog(LOG_INFO, "normalkey pressed: %c %d",  workstring[i+4],j);
							  cRemote::Put(LcdNormalMap[j]);
						  }
					  }
				  }
			  }
		  }

		  // Output

		  char lcdCommand[100];
		  if (!closing){
			  if (LcdSetup.OutputNumber > 0){
				  int OutValue = 0;
				  for (int o=0; o <  LcdSetup.OutputNumber; o++){
					  switch(LcdSetup.OutputFunction[o]){
					  case 0: // Off
						  break;
					  case 1: // On
						  OutValue += 1 << o;
						  break;
					  case 2: // Recording DVB 1
						  if (OutStateData.CardStat[0] == 2)
							  OutValue += 1 << o;
						  break;
					  case 3: // Recording DVB 2
						  if (OutStateData.CardStat[1] == 2)
							  OutValue += 1 << o;
						  break;
					  case 4: // Recording DVB 3
						  if (OutStateData.CardStat[2] == 2)
							  OutValue += 1 << o;
						  break;
					  case 5: // Recording DVB 4
						  if (OutStateData.CardStat[3] == 2)
							  OutValue += 1 << o;
						  break;
					  case 6: // Replay
						  if (OutStateData.State == Replay)
							  OutValue += 1 << o;
						  break;
					  case 7: // DVD
						  if ( (OutStateData.State == Replay) && (!strncmp(OutStateData.lcdfullbuffer[LCDREPLAY],"DVD", 3)))
							  OutValue += 1 << o;
						  break;
					  case 8: // Mplayer
						  break;
					  case 9: // MP3
						  if ( (OutStateData.State == Replay) && OutStateData.lcdfullbuffer[LCDREPLAY][0]=='[' &&
								  OutStateData.lcdfullbuffer[LCDREPLAY][3]==']' &&
								  (OutStateData.lcdfullbuffer[LCDREPLAY][1]=='.' || OutStateData.lcdfullbuffer[LCDREPLAY][1]=='L' ) &&
								  (OutStateData.lcdfullbuffer[LCDREPLAY][2]=='.' || OutStateData.lcdfullbuffer[LCDREPLAY][2]=='S' )
						  )
							  OutValue += 1 << o;
						  break;
					  case 10: // Mplayer + MP3
						  // Until I find a better solution any replay that is not a DVD is flagged as Mplayer-Mp3
						  if ( (OutStateData.State == Replay) && (strncmp(OutStateData.lcdfullbuffer[LCDREPLAY],"DVD",3))!= 0)
							  OutValue += 1 << o;
						  break;
					  case 11: // User1
						  break;
					  case 12: // User2
						  break;
					  case 13: // User3
						  break;
					  default:
						  ;
					  }
				  }
				  sprintf(lcdCommand,"output %d\n",OutValue);
				  sock_send_string(sock,lcdCommand);
			  }
		  }
		  usleep(WakeUpCycle-(now.tv_usec%WakeUpCycle)); // sync to systemtime for nicer time output
		  if (!Running()) return;
	  }
  }
}

void cLcd::ChannelSwitched() {
    channelSwitched = true;
}
