#ifndef __LCD_H
#define __LCD_H

#include <vdr/thread.h>
#include <vdr/device.h>
#include <vdr/player.h>
#include <vdr/dvbplayer.h>

#define LCDPORT 13666
#define LCDHOST "localhost"
#define LCDMAXSTATES 5
#define LCDMAXSTATEBUF 5
#define LCDMAXWID 40
#define LCDMAXCARDS 4

class cLcd : public cThread {
  public:
    cControl *replayDvbApi;	  
    cDevice *primaryDvbApi;	  
    enum ThreadStates {Menu=0, Title=1, Replay=2, Misc=3, Vol=4};
    ThreadStates LastState[LCDMAXSTATEBUF];
    int LastStateP,LineMode;
    struct StateData {
      char lcdbuffer[LCDMAXSTATES][4][LCDMAXWID+1];
      bool lcddirty[LCDMAXSTATES][4], muted, showvolume;
      ThreadStates State;
      unsigned int barx, bary, barl, volume;
      unsigned int CardStat[LCDMAXCARDS];
    };
    cLcd();
    ~cLcd();
    bool Connect( char *host = LCDHOST , unsigned int port = LCDPORT );
    void Close();
    void Info();
    void SetTitle( const char *string);
    void SetMain( unsigned int n, const char *string);
    void SetHelp( unsigned int n, const char *Red, const char *Green, const char *Yellow, const char *Blue);
    void SetStatus( const char *string);
    void SetWarning( const char *string);
    void ShowVolume(unsigned int vol, bool muted ); 
    void SetProgress(const char *begin=NULL, const char *end=NULL, int percent=0); 
    void SetLine(unsigned int n, unsigned int l, const char *string); 
    void SetLineC(unsigned int n, unsigned int l, const char *string); 
    void SetBuffer(unsigned int n,const char *l1=NULL,const char *l2=NULL,const char *l3=NULL,const char *l4=NULL);
    void SetRunning(bool nownext, const char *string1="\0", const char *string2="\0", const char *string3="\0"); 
    void SummaryInit(char *string); 
    void SummaryUp();
    void SummaryDown();
    void SummaryDisplay();
    void SetCardStat(unsigned int card, unsigned int stat);
    void Clear(unsigned int n);
    void SetThreadState( ThreadStates newstate );
    void PopThreadState();
    void SetReplayDevice(cControl *DvbApi);
    void SetPrimaryDevice(cDevice *DvbApi);
  private:
    char *SummaryText;
    unsigned int SummaryTextL;
    unsigned int SummaryCurrent;
    bool connected;
    struct StateData ThreadStateData;
    time_t LastProgress;
    cMutex CriticalArea;
    unsigned int sock, wid, hgt, cellwid, cellhgt;
    char StringBuffer[2*LCDMAXWID+1];
    void BeginMutualExclusion();
    void EndMutualExclusion();    
    void Copy(char *to, const char *from, unsigned int max);
    void Split(const char *string, char *string1, char *string2);
    void Write(int line, const char *string);
    void GetTimeDateStat( char *string, unsigned int OutStateData[] );
    void Action(void);
};

#endif //__LCD_H
