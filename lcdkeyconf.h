#ifndef LCD_KEYTBL_H
#define LCD_KEYTBL_H
// LCDproc keys

//const unsigned int  LcdMaxKeys  = 0;
const unsigned int  LcdMaxKeys  = 25;
const unsigned char LcdShiftKey = '#';
const unsigned char LcdUsedKeys[] = 

{   '0',    '1',     '2',    '3',   '4', 
    '5',    '6',     '7',    '8',   '9', 
    'q',    'w',     'e',    'r',   'm', 
    'o',    'b',     'x',    'c',   'a', 
    'y',    's',     'd',    'f',   '#'  };

const eKeys LcdNormalMap[] = 

{    k0,     k1,      k2,     k3,    k4,
     k5,     k6,      k7,     k8,    k9,
   kRed, kGreen, kYellow,  kBlue, kMenu,
    kOk,  kBack,   kLeft, kRight,   kUp,
  kDown, kVolDn,  kVolUp,  kMute, kNone  };

const eKeys  LcdShiftMap[] = 

{    kPlay,      kPause,  kStop,    kRecord, kFastFwd,   
  kFastRew,     kChanUp, kChanDn, kSchedule, kChannels,
   kTimers, kRecordings,  kSetup, kCommands, kNone,
    kUser1,      kUser2,  kUser3,    kUser4, kUser5,
    kUser6,      kUser7,  kUser8,    kUser9, kPower  };

#endif

