#ifndef LCD_SETUP_H
#define LCD_SETUP_H

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
public:
  cLcdSetup(void);
  };

extern cLcdSetup LcdSetup;

#endif
