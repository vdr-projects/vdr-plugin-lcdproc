#ifndef LCD_SETUP_H
#define LCD_SETUP_H

#define LCDMAXOUTPUTS 10

class cLcdSetup {
public:
  int FullCycle;
  int TimeCycle;
  int VolumeKeep;
  int Scrollwait;
  int Scrollspeed;	  
  int Charmap;	  
  int AltShift;	  
  int BackLight;	  
  int OutputNumber;
  int OutputFunction[LCDMAXOUTPUTS];
public:
  cLcdSetup(void);
  };


extern cLcdSetup LcdSetup;

#endif
