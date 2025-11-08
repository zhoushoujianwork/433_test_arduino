/*
 * ESP433RFWeb - 433MHz信号Web管理界面库
 * 
 * 提供WiFi AP模式和Web管理界面，支持信号列表查看、添加、删除、发送
 * 
 * Author: Zhoushoujian
 * License: MIT
 */

#ifndef ESP433RFWEB_H
#define ESP433RFWEB_H

#include <Arduino.h>
#include "ESP433RF.h"
#include "SignalManager.h"

#ifdef ESP32
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#endif

class ESP433RFWeb {
public:
  // 构造函数
  ESP433RFWeb(ESP433RF& rf, SignalManager& signalMgr);
  
  // 初始化
  void begin(const char* ssid = "ESP433RF", const char* password = "12345678");
  void end();
  void handleClient();  // 需要在loop中调用
  
  // WiFi配置
  void setAPCredentials(const char* ssid, const char* password);
  String getAPIP();
  bool isAPMode();
  
  // 捕获模式回调
  typedef void (*CaptureModeCallback)(bool enabled);
  void setCaptureModeCallback(CaptureModeCallback callback);
  
  // Boot按钮绑定
  int8_t getBootBoundIndex() { return _bootBoundIndex; }
  
private:
  ESP433RF& _rf;
  SignalManager& _signalMgr;
  
  #ifdef ESP32
  WebServer* _server;
  String _apSSID;
  String _apPassword;
  bool _apStarted;
  CaptureModeCallback _captureCallback;
  int8_t _bootBoundIndex;  // Boot按钮绑定的信号索引（-1表示未绑定）
  
  // Web路由处理函数
  void handleRoot();
  void handleAPI();
  void handleNotFound();
  void sendJSONResponse(int code, const String& message, const String& data = "");
  String getSignalListJSON();
  #endif
};

#endif // ESP433RFWEB_H

