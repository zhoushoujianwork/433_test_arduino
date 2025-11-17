/*
 * ESPMultiRF - ESP32 Multi-Frequency RF Transceiver Library
 * 
 * A universal library for receiving and transmitting 433MHz and 315MHz signals using ESP32
 * Uses RCSwitch library for 433MHz and TCSwitch for 315MHz
 * Supports various receiver modules (Ling-R1A, etc.)
 * Supports raw signal capture and replay for undecodable signals
 * 
 * Author: Zhoushoujian
 * License: MIT
 * Version: 2.0.0
 * 
 * Note: This library requires USE_RCSWITCH to be defined and RCSwitch library installed
 */

#ifndef ESPMultiRF_H
#define ESPMultiRF_H

#include <Arduino.h>
#include <HardwareSerial.h>

// This library requires RCSwitch - auto-define if not already defined
#ifndef USE_RCSWITCH
#define USE_RCSWITCH 1
#endif

#include <RCSwitch.h>
#include "TCSwitch.h"

// ESP32 Preferences for flash storage
#ifdef ESP32
#include <Preferences.h>
#endif

// 频率类型枚举
enum RFFrequency {
  RF_433MHZ = 0,  // 433MHz
  RF_315MHZ = 1   // 315MHz
};

// 信号结构体（解码后的地址码）
struct RFSignal {
  String address;  // 6-digit hex address code
  String key;      // 2-digit hex key value
  RFFrequency frequency;  // 频率类型
  uint8_t protocol;       // 协议编号
  uint16_t pulseLength;   // 脉冲长度
};

// 原始信号结构体（用于无法解码的信号）
struct RFRawSignal {
  unsigned int* timings;   // 时序数据数组（微秒）
  unsigned int changeCount; // 时序数据的变化次数
  RFFrequency frequency;   // 频率类型
  bool isValid;            // 是否有效
};

class ESPMultiRF {
public:
  // Constructor - 支持双频率引脚配置
  ESPMultiRF(uint8_t tx433Pin = 14, uint8_t rx433Pin = 18, 
             uint8_t tx315Pin = 15, uint8_t rx315Pin = 19, 
             uint32_t baudRate = 9600);
  
  // Initialization
  void begin();
  void end();
  
  // Receive functions
  bool receiveAvailable();
  bool receive(RFSignal &signal);
  bool parseSignal(String data, RFSignal &signal);
  
  // 接收原始信号（用于无法解码的情况）
  bool receiveRaw(RFRawSignal &rawSignal, RFFrequency freq = RF_433MHZ);
  
  // Send functions
  void send(String address, String key, RFFrequency freq = RF_433MHZ);
  void send(RFSignal signal);
  
  // 发送原始信号（用于无法解码的情况）
  void sendRaw(RFRawSignal &rawSignal, unsigned int repeatCount = 1);
  
  // Configuration
  void setRepeatCount(uint8_t count, RFFrequency freq = RF_433MHZ);
  void setProtocol(uint8_t protocol, RFFrequency freq = RF_433MHZ);
  void setPulseLength(uint16_t pulseLength, RFFrequency freq = RF_433MHZ);
  
  // 频率选择
  void setFrequency(RFFrequency freq);
  RFFrequency getFrequency() { return _currentFrequency; }
  
  // Status
  uint32_t getSendCount() { return _sendCount; }
  uint32_t getReceiveCount() { return _receiveCount; }
  void resetCounters();
  
  // Callback support
  typedef void (*ReceiveCallback)(RFSignal signal);
  typedef void (*RawReceiveCallback)(RFRawSignal rawSignal);
  void setReceiveCallback(ReceiveCallback callback);
  void setRawReceiveCallback(RawReceiveCallback callback);
  
  // Replay buffer functions (信号历史记录)
  void enableReplayBuffer(uint8_t size = 10);  // 启用复刻缓冲区
  void disableReplayBuffer();  // 禁用复刻缓冲区
  uint8_t getReplayBufferCount();  // 获取缓冲区信号数量
  bool getReplaySignal(uint8_t index, RFSignal &signal);  // 获取指定索引的信号
  RFSignal getLastReceived();  // 获取最后接收的信号
  void clearReplayBuffer();  // 清空缓冲区
  
  // 原始信号缓冲区
  void enableRawReplayBuffer(uint8_t size = 10);  // 启用原始信号缓冲区
  void disableRawReplayBuffer();  // 禁用原始信号缓冲区
  uint8_t getRawReplayBufferCount();  // 获取原始信号缓冲区数量
  bool getRawReplaySignal(uint8_t index, RFRawSignal &rawSignal);  // 获取指定索引的原始信号
  RFRawSignal getLastReceivedRaw();  // 获取最后接收的原始信号
  void clearRawReplayBuffer();  // 清空原始信号缓冲区
  
  // Capture mode functions (捕获模式)
  void enableCaptureMode(bool rawMode = false);  // 启用捕获模式（rawMode=true时捕获原始信号）
  void disableCaptureMode();  // 禁用捕获模式
  bool isCaptureMode();  // 是否处于捕获模式
  bool isRawCaptureMode();  // 是否处于原始信号捕获模式
  bool hasCapturedSignal();  // 是否有捕获的信号
  bool hasCapturedRawSignal();  // 是否有捕获的原始信号
  RFSignal getCapturedSignal();  // 获取捕获的信号
  RFRawSignal getCapturedRawSignal();  // 获取捕获的原始信号
  void clearCapturedSignal();  // 清空捕获的信号
  
  // Receive control functions (接收控制)
  void enableReceive(RFFrequency freq = RF_433MHZ);  // 启用接收
  void disableReceive(RFFrequency freq = RF_433MHZ);  // 禁用接收
  bool isReceiving(RFFrequency freq = RF_433MHZ);  // 是否正在接收
  
  // Flash persistence functions (闪存持久化，仅ESP32)
  #ifdef ESP32
  void enableFlashStorage(const char* namespace_name = "rf_replay");  // 启用闪存存储
  void disableFlashStorage();  // 禁用闪存存储
  bool saveToFlash();  // 保存捕获的信号到闪存
  bool loadFromFlash();  // 从闪存加载信号
  void clearFlash();  // 清空闪存
  #endif
  
private:
  // Hardware pins
  uint8_t _tx433Pin;
  uint8_t _rx433Pin;
  uint8_t _tx315Pin;
  uint8_t _rx315Pin;
  uint32_t _baudRate;
  
  // Serial port (用于433MHz接收模块)
  HardwareSerial* _serial;
  
  // Switch instances
  RCSwitch* _rcSwitch;  // 433MHz
  TCSwitch* _tcSwitch;  // 315MHz
  
  // Current frequency
  RFFrequency _currentFrequency;
  
  // Configuration
  uint8_t _repeatCount433;
  uint8_t _repeatCount315;
  uint8_t _protocol433;
  uint8_t _protocol315;
  uint16_t _pulseLength433;
  uint16_t _pulseLength315;
  
  // Statistics
  uint32_t _sendCount;
  uint32_t _receiveCount;
  
  // Callback
  ReceiveCallback _receiveCallback;
  RawReceiveCallback _rawReceiveCallback;
  
  // Replay buffer
  bool _replayBufferEnabled;
  RFSignal* _replayBuffer;
  uint8_t _replayBufferSize;
  uint8_t _replayBufferIndex;
  uint8_t _replayBufferCount;
  RFSignal _lastReceived;
  
  // Raw replay buffer
  bool _rawReplayBufferEnabled;
  RFRawSignal* _rawReplayBuffer;
  unsigned int** _rawReplayBufferTimings;  // 存储原始时序数据
  uint8_t _rawReplayBufferSize;
  uint8_t _rawReplayBufferIndex;
  uint8_t _rawReplayBufferCount;
  RFRawSignal _lastReceivedRaw;
  
  // Capture mode
  bool _captureMode;
  bool _rawCaptureMode;
  RFSignal _capturedSignal;
  RFRawSignal _capturedRawSignal;
  bool _hasCapturedSignal;
  bool _hasCapturedRawSignal;
  
  // Receive control
  bool _receiveEnabled433;
  bool _receiveEnabled315;
  
  // Flash storage (ESP32 only)
  #ifdef ESP32
  bool _flashStorageEnabled;
  Preferences* _preferences;
  String _flashNamespace;
  #endif
  
  // Internal functions
  uint8_t hexToNum(char c);
  void sendSignalRCSwitch(String address, String key);
  void sendSignalTCSwitch(String address, String key);
  void addToReplayBuffer(RFSignal signal);
  void addToRawReplayBuffer(RFRawSignal rawSignal);
  void checkCaptureMode(RFSignal signal);
  void checkRawCaptureMode(RFRawSignal rawSignal);
  void freeRawSignal(RFRawSignal &rawSignal);
  RFRawSignal copyRawSignal(RFRawSignal &src);
};

#endif // ESPMultiRF_H
