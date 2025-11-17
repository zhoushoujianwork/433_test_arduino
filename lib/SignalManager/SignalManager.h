/*
 * SignalManager - 433MHz信号管理库
 * 
 * 支持多个信号的存储、添加、删除、查询和持久化
 * 
 * Author: Zhoushoujian
 * License: MIT
 */

#ifndef SIGNAL_MANAGER_H
#define SIGNAL_MANAGER_H

#include <Arduino.h>
#include "ESPMultiRF.h"

#ifdef ESP32
#include <Preferences.h>
#endif

// 信号项结构（包含名称和信号数据）
struct SignalItem {
  String name;      // 信号名称（用户自定义）
  RFSignal signal;  // 信号数据
  uint32_t timestamp; // 捕获时间戳
};

class SignalManager {
public:
  // 构造函数
  SignalManager(uint8_t maxSignals = 50);
  ~SignalManager();
  
  // 初始化
  void begin();
  void end();
  
  // 信号管理
  bool addSignal(const String& name, const RFSignal& signal);
  bool addSignal(const RFSignal& signal);  // 自动生成名称
  bool removeSignal(uint8_t index);
  bool removeSignal(const String& name);
  bool updateSignal(uint8_t index, const String& name, const RFSignal& signal);
  bool getSignal(uint8_t index, SignalItem& item);
  bool getSignal(const String& name, SignalItem& item);
  uint8_t getCount();
  void clear();
  
  // 发送信号
  bool sendSignal(uint8_t index, ESPMultiRF& rf);
  bool sendSignal(const String& name, ESPMultiRF& rf);
  
  // 持久化存储（ESP32）
  #ifdef ESP32
  bool saveToFlash();
  bool loadFromFlash();
  void clearFlash();
  #endif
  
  // 获取所有信号（用于Web界面）
  bool getAllSignals(SignalItem* items, uint8_t maxCount);
  
private:
  uint8_t _maxSignals;
  SignalItem* _signals;
  uint8_t _count;
  
  #ifdef ESP32
  Preferences* _preferences;
  bool _flashEnabled;
  String _flashNamespace;
  #endif
  
  String generateAutoName(uint8_t index);
  void initFlash();
};

#endif // SIGNAL_MANAGER_H

