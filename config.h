#ifndef _CONFIG_H_
#define _CONFIG_H_

#define DEBUG       1
#define PIN_LED     2  //板载LED
#define PIN_UP      16
#define PIN_DOWN    12
#define PIN_LEFT    5
#define PIN_RIGHT   4
#define PIN_CUSTOM1 13
#define PIN_CUSTOM2 14
#define PIN_2812    0

//灯颜色枚举
enum LedColor
{
    LedColorRed = 0,
    LedColorGreen = 1,
    LedColorBlue = 2,
    LedColorBlack = 3,
    LedColorOff = 4
};

// Debug串口0，打印调试日志
#if (DEBUG)
HardwareSerial *DBGCOM = &Serial;
#define LOGD(...)                \
  do                             \
  {                              \
    DBGCOM->printf(__VA_ARGS__); \
    DBGCOM->println();           \
  } while (0)
#else
#define LOGD(...)
#endif

#endif