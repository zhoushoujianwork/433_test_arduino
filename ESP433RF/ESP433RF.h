/*
 * ESP433RF - ESP32 433MHz RF Transceiver Library
 * 
 * A universal library for receiving and transmitting 433MHz signals using ESP32
 * Uses RCSwitch library for reliable transmission
 * Supports various receiver modules (Ling-R1A, etc.)
 * 
 * Author: Zhoushoujian
 * License: MIT
 * Version: 1.0.0
 * 
 * Note: This library requires USE_RCSWITCH to be defined and RCSwitch library installed
 */

#ifndef ESP433RF_H
#define ESP433RF_H

#include <Arduino.h>
#include <HardwareSerial.h>

// This library requires RCSwitch - auto-define if not already defined
#ifndef USE_RCSWITCH
#define USE_RCSWITCH 1
#endif

#include <RCSwitch.h>

// ESP32 Preferences for flash storage
#ifdef ESP32
#include <Preferences.h>
#endif

// Signal structure
struct RFSignal {
  String address;  // 6-digit hex address code
  String key;      // 2-digit hex key value
};

class ESP433RF {
public:
  // Constructor
  ESP433RF(uint8_t txPin = 14, uint8_t rxPin = 18, uint32_t baudRate = 9600);
  
  // Initialization
  void begin();
  void end();
  
  // Receive functions
  bool receiveAvailable();
  bool receive(RFSignal &signal);
  bool parseSignal(String data, RFSignal &signal);
  
  // Send functions
  void send(String address, String key);
  void send(RFSignal signal);
  
  // Configuration (RCSwitch only)
  void setRepeatCount(uint8_t count);
  void setProtocol(uint8_t protocol);
  void setPulseLength(uint16_t pulseLength);
  
  // Status
  uint32_t getSendCount() { return _sendCount; }
  uint32_t getReceiveCount() { return _receiveCount; }
  void resetCounters();
  
  // Callback support
  typedef void (*ReceiveCallback)(RFSignal signal);
  void setReceiveCallback(ReceiveCallback callback);
  
  // Replay buffer functions (信号历史记录)
  void enableReplayBuffer(uint8_t size = 10);  // 启用复刻缓冲区
  void disableReplayBuffer();  // 禁用复刻缓冲区
  uint8_t getReplayBufferCount();  // 获取缓冲区信号数量
  bool getReplaySignal(uint8_t index, RFSignal &signal);  // 获取指定索引的信号
  RFSignal getLastReceived();  // 获取最后接收的信号
  void clearReplayBuffer();  // 清空缓冲区
  
  // Capture mode functions (捕获模式)
  void enableCaptureMode();  // 启用捕获模式
  void disableCaptureMode();  // 禁用捕获模式
  bool isCaptureMode();  // 是否处于捕获模式
  bool hasCapturedSignal();  // 是否有捕获的信号
  RFSignal getCapturedSignal();  // 获取捕获的信号
  void clearCapturedSignal();  // 清空捕获的信号
  
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
  uint8_t _txPin;
  uint8_t _rxPin;
  uint32_t _baudRate;
  
  // Serial port
  HardwareSerial* _serial;
  
  // RCSwitch instance
  RCSwitch* _rcSwitch;
  
  // Configuration
  uint8_t _repeatCount;
  uint8_t _protocol;
  uint16_t _pulseLength;
  
  // Statistics
  uint32_t _sendCount;
  uint32_t _receiveCount;
  
  // Callback
  ReceiveCallback _receiveCallback;
  
  // Replay buffer
  bool _replayBufferEnabled;
  RFSignal* _replayBuffer;
  uint8_t _replayBufferSize;
  uint8_t _replayBufferIndex;
  uint8_t _replayBufferCount;
  RFSignal _lastReceived;
  
  // Capture mode
  bool _captureMode;
  RFSignal _capturedSignal;
  bool _hasCapturedSignal;
  
  // Flash storage (ESP32 only)
  #ifdef ESP32
  bool _flashStorageEnabled;
  Preferences* _preferences;
  String _flashNamespace;
  #endif
  
  // Internal functions
  uint8_t hexToNum(char c);
  void sendSignalRCSwitch(String address, String key);
  void addToReplayBuffer(RFSignal signal);
  void checkCaptureMode(RFSignal signal);
};

#endif // ESP433RF_H
