#include "setup.h"

cLcdSetup LcdSetup;

cLcdSetup::cLcdSetup(void)
{
  FullCycle   =  10;
  TimeCycle   =   7;
  VolumeKeep  =  15;
  Scrollwait  =  10;
  Scrollspeed =   3;
  Charmap     =   0;
  AltShift    =   0; 
  BackLight   =   1; 
  ClientPrioN = 128; 
  ClientPrioH =  16; 
  SetPrio     =   0; 
  BackLightWait = 0; 
  OutputNumber   =  0;
  for (int i = 0; i < LCDMAXOUTPUTS; i++)
    OutputFunction[i] = 0;
  RecordingStatus = 0;
  ShowTime = 1;
}
