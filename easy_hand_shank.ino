#include "config.h"
#include <WiFiManager.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <Ticker.h>
#include <Adafruit_NeoPixel.h>
#include <Bounce2.h>
#include <ArduinoJson.h>


//---------------------------------------------------------------------------------------------------------------------
//断网定时器，setup和loop中多久没连上时间没连上网络则重启
Ticker timer;

//实例化WiFiUDP对象
WiFiUDP Udp;

WiFiManager wifiManager;

// loop过程中检测到wifi断开连接，则复位此标志位
bool isWiFiConnected = false;

//(灯总数,使用引脚,WS2812B一般都是800这个参数不用动)
Adafruit_NeoPixel WS2812B(1, PIN_2812, NEO_GRB + NEO_KHZ800);

unsigned char c1Old = 120;
unsigned char c2Old = 10;

Bounce2::Button CUSTOM1 = Bounce2::Button();
Bounce2::Button CUSTOM2 = Bounce2::Button();

// mqtt参数结构体定义及其变量定义
struct MqttConfig {
  char mqttServer[32];
  char mqttPort[6];
  char mqttUser[32];
  char mqttPass[32];
  char mqttSubTopic[32];
};

struct MqttConfig config = {
  "",
  "1883",
  "",
  "",
  "z2m"
};

//用于保存数据的标志
bool isSaveConfig = false;

//---------------------------------------------------------------------------------------------------------------------
/**
 * @description: 从文件系统中读取mqtt的配置参数
 */
void getMqttConfig() {
#define READ_STR_CONFIG_FROM_JSON(name) \
  if (strlen(doc[#name])) \
  strlcpy(config.name, doc[#name], sizeof(config.name))

#define READ_INT_CONFIG_FROM_JSON(name) \
  if (doc[#name]) \
  config.name = doc[#name]

  LOGD("\n");

  StaticJsonDocument<512> doc;

  if (!SPIFFS.begin()) {
    LOGD("[ERROR] Mount spiffs failed.");
    return;
  }

  File f = SPIFFS.open("/mqtt.json", "r");
  if (!f) {
    LOGD("[ERROR] Config file not exist.");
    SPIFFS.end();
    return;
  }

  DeserializationError error = deserializeJson(doc, f);
  if (error) {
    LOGD("[ERROR] Failed to read file, using default configuration.");
    SPIFFS.end();
    return;
  }

  READ_STR_CONFIG_FROM_JSON(mqttServer);
  serializeJson(doc, *DBGCOM);
  SPIFFS.end();
}

/**
 * @description: Web设置wifi界面点击保存的回调函数
 */
void saveConfigCallback() {
  LOGD("Should save config");
  isSaveConfig = true;
}


/**
 * @description: 配置灯的颜色
 * @param color：LedColor
 */
void ledShowColor(LedColor color) {
  switch (color) {
    case LedColorRed:
      singleLedColor(0, 255, 0, 0);
      break;

    case LedColorGreen:
      singleLedColor(0, 0, 255, 0);
      break;

    case LedColorBlue:
      singleLedColor(0, 0, 0, 255);
      break;

    case LedColorBlack:
      singleLedColor(0, 0, 0, 0);
      break;

    case LedColorOff:
      WS2812B.clear();
      WS2812B.show();
      break;

    default:
      break;
  }
}

/**
 * @description: 设置灯带中某一个灯的颜色
 * @param index：灯的下标，从0开始
 * @param R：红 0~255
 * @param G：绿 0~255
 * @param B：蓝 0~255
 */
void singleLedColor(int index, int R, int G, int B) {
  WS2812B.setPixelColor(index, (((R & 0xffffff) << 16) | ((G & 0xffffff) << 8) | B));
  WS2812B.show();
}

/**
 * @description: 复位8266模组
 */
void resetESP8266() {
  ESP.reset();
}

/**
 * @description：检查网络状态，断网亮红灯，重新联网亮绿灯
 */
bool checkNetwork() {
  if (WiFi.status() != WL_CONNECTED) {
    //通过标志位，loop期间断网不要重复设置灯红色，只要设置一次即可，网络恢复同理也不要灭灯多次
    if (isWiFiConnected) {
      isWiFiConnected = false;
      digitalWrite(PIN_LED, HIGH);      
      ledShowColor(LedColorRed);
      LOGD("[ERROR] Wifi disconnect");
      //开启定时器，10分钟内没连网成功则重启
      timer.once(60 * 10, resetESP8266);
    }
  } else {
    if (!isWiFiConnected) {
      digitalWrite(PIN_LED, LOW);
      ledShowColor(LedColorGreen);
      LOGD("Wifi connect");
      timer.detach();
    }
    isWiFiConnected = true;
  }

  return isWiFiConnected;
}

/**
 * @description: PB4按键中断处理函数
 */
ICACHE_RAM_ATTR void c1BtnHandler() {
  static bool c1Press = false;
  c1Press = !c1Press;
  c1Old = c1Press ? 0 : 120;
}

/**
 * @description: PB4按键中断处理函数
 */
ICACHE_RAM_ATTR void c2BtnHandler() {
  static bool c2Press = false;
  c2Press = !c2Press;
  c2Old = c2Press ? 10 : 70;
}

void connectToWiFi() {
  wifiManager.setConnectTimeout(5);

  //从文件系统中读取mqtt的配置参数，以便带到web配网界面中显示
  getMqttConfig();
  WiFiManagerParameter custom_mqtt_server("server", "手柄热点名称", config.mqttServer, sizeof(config.mqttServer));
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.addParameter(&custom_mqtt_server);

  String apName;
  if (strlen(config.mqttServer) > 0) {
    apName = String(config.mqttServer);
  } else {
    apName = String("HandShank_") + String(ESP.getChipId());
  }
  wifiManager.setHostname(apName);
  LOGD("\n\n Begin connect wifi, apName = %s", apName.c_str());

  //开启定时器，5分钟内没连网成功则重启
  timer.once(60 * 5, resetESP8266);

  //网络设置界面exit会走返回这个false。web界面wifi密码连接错误此方法不会返回，还是重新变回ap
  if (!wifiManager.autoConnect(apName.c_str())) {
    LOGD("[ERROR] Failed to connect, now reset");
    delay(100);
    ESP.reset();
  }

  timer.detach();
  isWiFiConnected = true;
  digitalWrite(PIN_LED, LOW);
  ledShowColor(LedColorGreen);
  LOGD("Connect wifi success: %s", WiFi.localIP().toString().c_str());

  //点击保存，连网成功后才保存mqtt配置，不是点击保存时就保存。且如果上电没有走UI配网流程直接连网了也不保存。
  if (isSaveConfig) {
    LOGD("Saving config");
    strlcpy(config.mqttServer, custom_mqtt_server.getValue(), sizeof(config.mqttServer));

    DynamicJsonDocument doc(512);
    doc["mqttServer"] = config.mqttServer;

    if (SPIFFS.begin()) {
      File configFile = SPIFFS.open("/mqtt.json", "w");
      if (configFile) {
        serializeJson(doc, *DBGCOM);
        serializeJson(doc, configFile);
        configFile.close();
      } else {
        LOGD("[ERROR] Failed to open config file for writing");
      }
      SPIFFS.end();
    } else {
      LOGD("[ERROR] Mount spiffs failed.");
    }
  }
}


//---------------------------------------------------------------------------------------------------------------------
/**
 * @description: setup
 */
void setup() {
  //配置LED灯，未联网红色、联网绿色、node连进来蓝色
  WS2812B.begin();
  WS2812B.clear();
  WS2812B.setBrightness(130);
  ledShowColor(LedColorRed);

  /*
  引脚配置
  */
  pinMode(PIN_UP, INPUT);
  pinMode(PIN_DOWN, INPUT);
  pinMode(PIN_LEFT, INPUT);
  pinMode(PIN_RIGHT, INPUT);
  pinMode(PIN_CUSTOM1, INPUT);
  pinMode(PIN_CUSTOM2, INPUT);

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);

  //digitalPinToInterrupt IO值大于16不会进中断,arduino的坑要小心换小于16的IO口才有中断
  // attachInterrupt(digitalPinToInterrupt(PIN_CUSTOM1), c1BtnHandler, FALLING);
  // attachInterrupt(digitalPinToInterrupt(PIN_CUSTOM2), c2BtnHandler, FALLING);

  //按键防抖 当长按用
  CUSTOM1.attach(PIN_CUSTOM1, INPUT);
  CUSTOM2.attach(PIN_CUSTOM2, INPUT);
  CUSTOM1.interval(10);
  CUSTOM2.interval(10);
  CUSTOM1.setPressedState(LOW);
  CUSTOM2.setPressedState(LOW);

#if (DEBUG)
  DBGCOM->begin(115200);
#endif

  /*
  上次如果有配置过wifi账号密码，启动后autoConnect如果连接不成功会等待10s，10s内把路由那边的wifi密码开起来还能连上不会自动变AP，
  不设这个超时连接失败是立刻变AP
  */
  connectToWiFi();
}

/**
 * @description: loop
 */
void loop() {
  /*
  TODO1:按键消抖 
  https://getiot.tech/arduino/arduino-button-debounce.html

  这里有 udp 的测试工具，或者先打日志调试，然后直接上小车
  http://www.taichi-maker.com/homepage/iot-development/iot-dev-reference/esp8266-c-plus-plus-reference/wifiudp/esp8266-udp-led/
  */

  if (!checkNetwork()) {
    return;
  }

  //读取6个按键状态，默认高电平，按下低电平
  unsigned char buf[20] = { 0 };
  int up = digitalRead(PIN_UP);
  int down = digitalRead(PIN_DOWN);
  int left = digitalRead(PIN_LEFT);
  int right = digitalRead(PIN_RIGHT);

  //以下的 100 的值待与小车调试之后再调整，暂定100
  if ((up == 0) && (down == 1)) {
    buf[1] = -100;
  } else if ((down == 0) && (up == 1)) {
    buf[1] = 100;
  }

  if ((left == 0) && (right == 1)) {
    buf[2] = -100;
  } else if ((right == 0) && (left == 1)) {
    buf[2] = 100;
  }

  //舵机对应的 custom1 custom2 对应 buf[4] buf[5] 先不调试，先调前进后退和转弯
  CUSTOM1.update();
  CUSTOM2.update();
  if (CUSTOM1.pressed()) {
    c1Old = (c1Old == 120) ? 0 : 120;
  }
  if (CUSTOM2.pressed()) {
    c2Old = (c2Old == 70) ? 10 : 70;
  }
  buf[4] = c1Old;
  buf[5] = c2Old;

  //双键长按3s清除wifi配置并重启
  bool c1Value = CUSTOM1.read();
  unsigned long c1Duration = CUSTOM1.duration();
  bool c2Value = CUSTOM2.read();
  unsigned long c2Duration = CUSTOM2.duration();
  if (!c1Value && !c2Value && (c1Duration >= 3000) && (c2Duration >= 3000)) {
    wifiManager.resetSettings();
    delay(50);
    resetESP8266();
  }

  //UDP发送，这里的IP注意如果是与小车连，和贤镇确认下小车是不是这个IP；如果是与PC的udp调试工具连，那么是填写PC的WIFI的IP
  Udp.beginPacket("192.168.4.1", 9090);
  Udp.write(buf, sizeof(buf));
  Udp.endPacket();

  //这里延时将来改成30，调试先用1000
  delay(30);
}
