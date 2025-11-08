/*
 * ESP433RF - ESP32 433MHz RF Transceiver Library
 * Implementation - RCSwitch only
 */

#include "ESP433RF.h"

// Constructor
ESP433RF::ESP433RF(uint8_t txPin, uint8_t rxPin, uint32_t baudRate) {
  _txPin = txPin;
  _rxPin = rxPin;
  _baudRate = baudRate;
  _serial = &Serial1;
  _repeatCount = 5;
  _protocol = 1;
  _pulseLength = 320;
  _sendCount = 0;
  _receiveCount = 0;
  _receiveCallback = nullptr;
  _rcSwitch = nullptr;
  
  // Initialize replay buffer
  _replayBufferEnabled = false;
  _replayBuffer = nullptr;
  _replayBufferSize = 0;
  _replayBufferIndex = 0;
  _replayBufferCount = 0;
  _lastReceived = {"", ""};
  
  // Initialize capture mode
  _captureMode = false;
  _capturedSignal = {"", ""};
  _hasCapturedSignal = false;
  
  // Initialize receive control
  _receiveEnabled = true;
  
  // Initialize flash storage
  #ifdef ESP32
  _flashStorageEnabled = false;
  _preferences = nullptr;
  _flashNamespace = "rf_replay";
  #endif
}

// Begin initialization
void ESP433RF::begin() {
  // Initialize TX pin
  pinMode(_txPin, OUTPUT);
  digitalWrite(_txPin, LOW);
  
  // Initialize RX serial
  _serial->begin(_baudRate, SERIAL_8N1, _rxPin, -1);
  
  // Initialize RCSwitch
  if (_rcSwitch == nullptr) {
    _rcSwitch = new RCSwitch();
    _rcSwitch->enableTransmit(_txPin);
    _rcSwitch->setProtocol(_protocol);
    _rcSwitch->setPulseLength(_pulseLength);
    _rcSwitch->setRepeatTransmit(_repeatCount);
  }
  
  resetCounters();
}

// End
void ESP433RF::end() {
  if (_rcSwitch != nullptr) {
    delete _rcSwitch;
    _rcSwitch = nullptr;
  }
  _serial->end();
}

// Check if receive data is available
bool ESP433RF::receiveAvailable() {
  return _serial->available() > 0;
}

// Receive signal
bool ESP433RF::receive(RFSignal &signal) {
  static String buffer = "";
  
  // 如果接收被禁用，清空缓冲区并返回false
  if (!_receiveEnabled) {
    while (_serial->available()) {
      _serial->read();  // 丢弃数据
    }
    return false;
  }
  
  while (_serial->available()) {
    char c = _serial->read();
    
    if (c == '\n' || c == '\r') {
      if (buffer.length() > 0) {
        // 调试输出：显示接收到的原始数据
        Serial.printf("[ESP433RF] 接收原始数据: %s\n", buffer.c_str());
        bool result = parseSignal(buffer, signal);
        if (result) {
          Serial.printf("[ESP433RF] 解析结果: 地址码=%s, 按键值=%s (完整数据=%s%s)\n", 
                       signal.address.c_str(), signal.key.c_str(),
                       signal.address.c_str(), signal.key.c_str());
        }
        buffer = "";
        if (result) {
          _receiveCount++;
          
          // 添加到复刻缓冲区
          addToReplayBuffer(signal);
          
          // 检查捕获模式
          checkCaptureMode(signal);
          
          if (_receiveCallback != nullptr) {
            _receiveCallback(signal);
          }
          return true;
        }
      }
    } else {
      buffer += c;
      if (buffer.length() > 64) {
        buffer = "";  // Buffer overflow protection
      }
    }
  }
  
  return false;
}

// Parse signal from string
bool ESP433RF::parseSignal(String data, RFSignal &signal) {
  data.trim();
  
  // Format 1: LC:XXXXXXYY
  if (data.startsWith("LC:") && data.length() >= 11) {
    String signalStr = data.substring(3, 11);
    signal.address = signalStr.substring(0, 6);
    signal.key = signalStr.substring(6, 8);
    signal.address.toUpperCase();
    signal.key.toUpperCase();
    return true;
  }
  
  // Format 2: RX:XXXXXXYY
  if (data.startsWith("RX:") && data.length() >= 11) {
    String signalStr = data.substring(3, 11);
    signal.address = signalStr.substring(0, 6);
    signal.key = signalStr.substring(6, 8);
    signal.address.toUpperCase();
    signal.key.toUpperCase();
    return true;
  }
  
  // Format 3: Direct 8-digit hex
  if (data.length() >= 8) {
    bool isHex = true;
    for (int i = 0; i < 8; i++) {
      char c = data.charAt(i);
      if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) {
        isHex = false;
        break;
      }
    }
    if (isHex) {
      signal.address = data.substring(0, 6);
      signal.key = data.substring(6, 8);
      signal.address.toUpperCase();
      signal.key.toUpperCase();
      return true;
    }
  }
  
  return false;
}

// Send signal
void ESP433RF::send(String address, String key) {
  _sendCount++;
  sendSignalRCSwitch(address, key);
}

// Send signal (RFSignal struct)
void ESP433RF::send(RFSignal signal) {
  send(signal.address, signal.key);
}

// Set repeat count
void ESP433RF::setRepeatCount(uint8_t count) {
  _repeatCount = count;
  if (_rcSwitch != nullptr) {
    _rcSwitch->setRepeatTransmit(count);
  }
}

// Set protocol
void ESP433RF::setProtocol(uint8_t protocol) {
  _protocol = protocol;
  if (_rcSwitch != nullptr) {
    _rcSwitch->setProtocol(protocol);
  }
}

// Set pulse length
void ESP433RF::setPulseLength(uint16_t pulseLength) {
  _pulseLength = pulseLength;
  if (_rcSwitch != nullptr) {
    _rcSwitch->setPulseLength(pulseLength);
  }
}

// Reset counters
void ESP433RF::resetCounters() {
  _sendCount = 0;
  _receiveCount = 0;
}

// Set receive callback
void ESP433RF::setReceiveCallback(ReceiveCallback callback) {
  _receiveCallback = callback;
}

// Convert hex character to number
uint8_t ESP433RF::hexToNum(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return 0;
}

// Send signal via RCSwitch
void ESP433RF::sendSignalRCSwitch(String address, String key) {
  if (_rcSwitch == nullptr) return;
  
  // RCSwitch方式：将地址码和按键值转换为24位数据
  // 注意：1527编码是24位，RCSwitch只支持24位
  // 接收模块返回的格式：地址码（6位十六进制）+ 按键值（2位十六进制）= 8位十六进制
  // 例如：E7E83100 = 地址码E7E831 + 按键值00
  // 这8位数据组合后就是24位数据，直接发送即可
  
  String fullHex = address + key;  // 例如：E7E83100 (8位十六进制)
  
  // 解析为32位数据
  uint32_t fullData = 0;
  for (int i = 0; i < 8 && i < fullHex.length(); i++) {
    uint8_t val = hexToNum(fullHex.charAt(i));
    fullData = (fullData << 4) | val;
  }
  
  // 1527编码格式：24位数据
  // 接收模块返回格式：地址码（6位十六进制）+ 按键值（2位十六进制）= 8位十六进制
  // 例如：62E7E831 = 地址码62E7E8（24位） + 按键值31（8位）
  // 
  // 为什么地址码可以是8位但很多信号助手显示6位？
  // 1. 433MHz编码协议（EV1527/PT2262）只支持24位数据
  // 2. 24位 = 6位十六进制（6 × 4 = 24）
  // 3. 显示方式差异：
  //    - 6位显示：只显示地址码（如 62E7E8），这是433MHz编码的实际有效位
  //    - 8位显示：地址码（6位）+ 按键值（2位），如 62E7E831
  //      但实际只有前24位（地址码）有效，后8位（按键值）可能用于其他用途
  // 4. 接收模块可能返回8位十六进制（32位），但协议只支持24位
  //    所以发送时只使用前24位（地址码），忽略后8位（按键值）
  //
  // 提取24位数据：取前24位（地址码），去掉后8位（按键值）
  // 例如：0x62E7E831 -> 右移8位 -> 0x0062E7E8（前24位，地址码）
  uint32_t code24bit = (fullData >> 8) & 0xFFFFFF;  // 前24位（地址码）
  
  // 调试输出：显示发送的数据
  Serial.printf("[ESP433RF] 发送数据 - 输入:%s%s (32位:0x%08lX), 24位:0x%06lX\n", 
               address.c_str(), key.c_str(), fullData, code24bit);
  
  // 确保RCSwitch配置正确（每次发送前检查）
  _rcSwitch->setProtocol(_protocol);
  _rcSwitch->setPulseLength(_pulseLength);
  _rcSwitch->setRepeatTransmit(_repeatCount);
  
  // 发送24位数据
  _rcSwitch->send(code24bit, 24);
  
  Serial.printf("[ESP433RF] 已发送24位数据: 0x%06lX (重复%d次)\n", code24bit, _repeatCount);
}

// ========== Replay Buffer Functions ==========

void ESP433RF::enableReplayBuffer(uint8_t size) {
  if (_replayBuffer != nullptr) {
    delete[] _replayBuffer;
  }
  _replayBufferSize = size;
  _replayBuffer = new RFSignal[size];
  _replayBufferIndex = 0;
  _replayBufferCount = 0;
  _replayBufferEnabled = true;
}

void ESP433RF::disableReplayBuffer() {
  if (_replayBuffer != nullptr) {
    delete[] _replayBuffer;
    _replayBuffer = nullptr;
  }
  _replayBufferEnabled = false;
  _replayBufferSize = 0;
  _replayBufferIndex = 0;
  _replayBufferCount = 0;
}

uint8_t ESP433RF::getReplayBufferCount() {
  return _replayBufferCount;
}

bool ESP433RF::getReplaySignal(uint8_t index, RFSignal &signal) {
  if (!_replayBufferEnabled || _replayBuffer == nullptr || index >= _replayBufferCount) {
    return false;
  }
  int startIdx = (_replayBufferIndex - _replayBufferCount + _replayBufferSize) % _replayBufferSize;
  int idx = (startIdx + index) % _replayBufferSize;
  signal = _replayBuffer[idx];
  return true;
}

RFSignal ESP433RF::getLastReceived() {
  return _lastReceived;
}

void ESP433RF::clearReplayBuffer() {
  _replayBufferIndex = 0;
  _replayBufferCount = 0;
}

void ESP433RF::addToReplayBuffer(RFSignal signal) {
  if (!_replayBufferEnabled || _replayBuffer == nullptr) {
    return;
  }
  _lastReceived = signal;
  _replayBuffer[_replayBufferIndex] = signal;
  _replayBufferIndex = (_replayBufferIndex + 1) % _replayBufferSize;
  if (_replayBufferCount < _replayBufferSize) {
    _replayBufferCount++;
  }
}

// ========== Capture Mode Functions ==========

void ESP433RF::enableCaptureMode() {
  _captureMode = true;
  _hasCapturedSignal = false;
  _capturedSignal = {"", ""};
}

void ESP433RF::disableCaptureMode() {
  _captureMode = false;
}

bool ESP433RF::isCaptureMode() {
  return _captureMode;
}

bool ESP433RF::hasCapturedSignal() {
  return _hasCapturedSignal;
}

RFSignal ESP433RF::getCapturedSignal() {
  return _capturedSignal;
}

void ESP433RF::clearCapturedSignal() {
  _hasCapturedSignal = false;
  _capturedSignal = {"", ""};
  #ifdef ESP32
  if (_flashStorageEnabled) {
    clearFlash();
  }
  #endif
}

void ESP433RF::checkCaptureMode(RFSignal signal) {
  if (_captureMode) {
    _capturedSignal = signal;
    _hasCapturedSignal = true;
    _captureMode = false;  // 捕获完成后自动退出捕获模式
    
    #ifdef ESP32
    if (_flashStorageEnabled) {
      saveToFlash();
    }
    #endif
  }
}

// ========== Flash Storage Functions (ESP32 only) ==========

#ifdef ESP32
void ESP433RF::enableFlashStorage(const char* namespace_name) {
  _flashStorageEnabled = true;
  _flashNamespace = String(namespace_name);
  if (_preferences == nullptr) {
    _preferences = new Preferences();
  }
}

void ESP433RF::disableFlashStorage() {
  _flashStorageEnabled = false;
  if (_preferences != nullptr) {
    delete _preferences;
    _preferences = nullptr;
  }
}

bool ESP433RF::saveToFlash() {
  if (!_flashStorageEnabled || _preferences == nullptr) {
    return false;
  }
  
  _preferences->begin(_flashNamespace.c_str(), false);
  if (_hasCapturedSignal && _capturedSignal.address.length() > 0) {
    _preferences->putString("address", _capturedSignal.address);
    _preferences->putString("key", _capturedSignal.key);
    _preferences->putBool("captured", true);
    _preferences->end();
    return true;
  } else {
    _preferences->remove("address");
    _preferences->remove("key");
    _preferences->putBool("captured", false);
    _preferences->end();
    return false;
  }
}

bool ESP433RF::loadFromFlash() {
  if (!_flashStorageEnabled || _preferences == nullptr) {
    return false;
  }
  
  _preferences->begin(_flashNamespace.c_str(), true);
  bool saved = _preferences->getBool("captured", false);
  if (saved) {
    _capturedSignal.address = _preferences->getString("address", "");
    _capturedSignal.key = _preferences->getString("key", "");
    if (_capturedSignal.address.length() > 0 && _capturedSignal.key.length() > 0) {
      _hasCapturedSignal = true;
      _preferences->end();
      return true;
    }
  }
  _hasCapturedSignal = false;
  _preferences->end();
  return false;
}

void ESP433RF::clearFlash() {
  if (!_flashStorageEnabled || _preferences == nullptr) {
    return;
  }
  _preferences->begin(_flashNamespace.c_str(), false);
  _preferences->remove("address");
  _preferences->remove("key");
  _preferences->putBool("captured", false);
  _preferences->end();
}
#endif

// Receive control functions
void ESP433RF::enableReceive() {
  _receiveEnabled = true;
  Serial.println("[ESP433RF] 接收已启用");
}

void ESP433RF::disableReceive() {
  _receiveEnabled = false;
  Serial.println("[ESP433RF] 接收已禁用");
}

bool ESP433RF::isReceiving() {
  return _receiveEnabled;
}
