/*
 * ESPMultiRF - ESP32 Multi-Frequency RF Transceiver Library
 * Implementation - RCSwitch (433MHz) and TCSwitch (315MHz)
 * Supports raw signal capture and replay
 */

#include "ESPMultiRF.h"

// Constructor - 支持双频率引脚配置
ESPMultiRF::ESPMultiRF(uint8_t tx433Pin, uint8_t rx433Pin, 
                       uint8_t tx315Pin, uint8_t rx315Pin, 
                       uint32_t baudRate) {
  _tx433Pin = tx433Pin;
  _rx433Pin = rx433Pin;
  _tx315Pin = tx315Pin;
  _rx315Pin = rx315Pin;
  _baudRate = baudRate;
  _serial = &Serial1;
  _currentFrequency = RF_433MHZ;
  
  // 433MHz 配置
  _repeatCount433 = 5;
  _protocol433 = 1;
  _pulseLength433 = 320;
  
  // 315MHz 配置
  _repeatCount315 = 5;
  _protocol315 = 1;
  _pulseLength315 = 320;
  
  _sendCount = 0;
  _receiveCount = 0;
  _receiveCallback = nullptr;
  _rawReceiveCallback = nullptr;
  _rcSwitch = nullptr;
  _tcSwitch = nullptr;
  
  // Initialize replay buffer
  _replayBufferEnabled = false;
  _replayBuffer = nullptr;
  _replayBufferSize = 0;
  _replayBufferIndex = 0;
  _replayBufferCount = 0;
  _lastReceived = {"", "", RF_433MHZ, 1, 320};
  
  // Initialize raw replay buffer
  _rawReplayBufferEnabled = false;
  _rawReplayBuffer = nullptr;
  _rawReplayBufferTimings = nullptr;
  _rawReplayBufferSize = 0;
  _rawReplayBufferIndex = 0;
  _rawReplayBufferCount = 0;
  _lastReceivedRaw = {nullptr, 0, RF_433MHZ, false};
  
  // Initialize capture mode
  _captureMode = false;
  _rawCaptureMode = false;
  _capturedSignal = {"", "", RF_433MHZ, 1, 320};
  _capturedRawSignal = {nullptr, 0, RF_433MHZ, false};
  _hasCapturedSignal = false;
  _hasCapturedRawSignal = false;
  
  // Initialize receive control
  _receiveEnabled433 = true;
  _receiveEnabled315 = true;
  
  // Initialize flash storage
  #ifdef ESP32
  _flashStorageEnabled = false;
  _preferences = nullptr;
  _flashNamespace = "rf_replay";
  #endif
}

// Begin initialization
void ESPMultiRF::begin() {
  // Initialize 433MHz TX pin
  pinMode(_tx433Pin, OUTPUT);
  digitalWrite(_tx433Pin, LOW);
  
  // Initialize 315MHz TX pin
  pinMode(_tx315Pin, OUTPUT);
  digitalWrite(_tx315Pin, LOW);
  
  // Initialize RX serial (用于433MHz接收模块)
  _serial->begin(_baudRate, SERIAL_8N1, _rx433Pin, -1);
  
  // Initialize RCSwitch (433MHz)
  if (_rcSwitch == nullptr) {
    _rcSwitch = new RCSwitch();
    _rcSwitch->enableTransmit(_tx433Pin);
    _rcSwitch->setProtocol(_protocol433);
    _rcSwitch->setPulseLength(_pulseLength433);
    _rcSwitch->setRepeatTransmit(_repeatCount433);
  }
  
  // Initialize TCSwitch (315MHz)
  if (_tcSwitch == nullptr) {
    _tcSwitch = new TCSwitch();
    _tcSwitch->enableTransmit(_tx315Pin);
    _tcSwitch->setProtocol(_protocol315);
    _tcSwitch->setPulseLength(_pulseLength315);
    _tcSwitch->setRepeatTransmit(_repeatCount315);
    
    // 启用315MHz接收（使用中断）
    _tcSwitch->enableReceive(_rx315Pin);
  }
  
  resetCounters();
}

// End
void ESPMultiRF::end() {
  if (_rcSwitch != nullptr) {
    delete _rcSwitch;
    _rcSwitch = nullptr;
  }
  if (_tcSwitch != nullptr) {
    _tcSwitch->disableReceive();
    delete _tcSwitch;
    _tcSwitch = nullptr;
  }
  _serial->end();
  
  // 清理原始信号缓冲区
  clearRawReplayBuffer();
}

// Check if receive data is available
bool ESPMultiRF::receiveAvailable() {
  // 检查433MHz串口接收
  if (_serial->available() > 0) {
    return true;
  }
  
  // 检查315MHz中断接收
  if (_tcSwitch != nullptr && _tcSwitch->available()) {
    return true;
  }
  
  return false;
}

// Receive signal
bool ESPMultiRF::receive(RFSignal &signal) {
  static String buffer = "";
  static String rawBytes = "";  // 用于存储原始字节的十六进制表示
  
  // 先检查315MHz中断接收
  if (_tcSwitch != nullptr && _receiveEnabled315 && _tcSwitch->available()) {
    unsigned long value = _tcSwitch->getReceivedValue();
    unsigned int bitlength = _tcSwitch->getReceivedBitlength();
    unsigned int protocol = _tcSwitch->getReceivedProtocol();
    unsigned int delay = _tcSwitch->getReceivedDelay();
    
    if (value > 0 && bitlength > 0) {
      // 转换为十六进制字符串
      char hexStr[9];
      sprintf(hexStr, "%08lX", value);
      String hexValue = String(hexStr);
      
      // 提取地址码和按键值（根据位长度）
      if (bitlength >= 24) {
        signal.address = hexValue.substring(0, 6);
        signal.key = hexValue.substring(6, 8);
      } else {
        // 位长度不足24位，只取有效部分
        int hexLen = (bitlength + 3) / 4;  // 转换为十六进制长度
        signal.address = hexValue.substring(0, min(6, hexLen));
        signal.key = hexValue.substring(min(6, hexLen), min(8, hexLen + 2));
      }
      
      signal.frequency = RF_315MHZ;
      signal.protocol = protocol;
      signal.pulseLength = delay;
      
      _receiveCount++;
      _lastReceived = signal;
      
      // 添加到复刻缓冲区
      addToReplayBuffer(signal);
      
      // 检查捕获模式
      checkCaptureMode(signal);
      
      if (_receiveCallback != nullptr) {
        _receiveCallback(signal);
      }
      
      _tcSwitch->resetAvailable();
      return true;
    }
    _tcSwitch->resetAvailable();
  }
  
  // 检查433MHz串口接收
  // 如果接收被禁用，清空缓冲区并返回false
  if (!_receiveEnabled433) {
    while (_serial->available()) {
      _serial->read();  // 丢弃数据
    }
    return false;
  }
  
  int availableBytes = _serial->available();
  if (availableBytes > 0) {
    Serial.printf("[SIGNAL_DEBUG] 串口可用字节数: %d\n", availableBytes);
  }
  
  while (_serial->available()) {
    char c = _serial->read();
    
    // 显示接收到的每个字节的详细信息
    Serial.printf("[SIGNAL_DEBUG] 接收字节: 0x%02X (%c) [ASCII: %d]\n", 
                 (uint8_t)c, (c >= 32 && c < 127) ? c : '.', (int)c);
    
    // 添加到原始字节字符串（用于调试）
    char hexStr[4];
    sprintf(hexStr, "%02X ", (uint8_t)c);
    rawBytes += hexStr;
    if (rawBytes.length() > 200) {
      rawBytes = rawBytes.substring(rawBytes.length() - 200);  // 只保留最后200个字符
    }
    
    if (c == '\n' || c == '\r') {
      if (buffer.length() > 0) {
        // 调试输出：显示接收到的完整原始数据
        Serial.println("========================================");
        Serial.printf("[SIGNAL_DEBUG] 完整数据包接收完成\n");
        Serial.printf("[SIGNAL_DEBUG] 原始字符串: [%s]\n", buffer.c_str());
        Serial.printf("[SIGNAL_DEBUG] 字符串长度: %d 字节\n", buffer.length());
        Serial.printf("[SIGNAL_DEBUG] 原始字节(HEX): %s\n", rawBytes.c_str());
        
        // 显示每个字符的详细信息
        Serial.printf("[SIGNAL_DEBUG] 字符详情: ");
        for (int i = 0; i < buffer.length(); i++) {
          char ch = buffer.charAt(i);
          Serial.printf("[%d]=0x%02X('%c') ", i, (uint8_t)ch, 
                       (ch >= 32 && ch < 127) ? ch : '.');
        }
        Serial.println();
        
        Serial.printf("[SIGNAL_DEBUG] 当前捕获模式状态: _captureMode=%d, _receiveEnabled433=%d\n",
                     _captureMode, _receiveEnabled433);
        Serial.println("========================================");
        
        bool result = parseSignal(buffer, signal);
        if (result) {
          Serial.printf("[SIGNAL_DEBUG] ✓ 解析成功: 地址码=%s, 按键值=%s (完整数据=%s%s)\n", 
                       signal.address.c_str(), signal.key.c_str(),
                       signal.address.c_str(), signal.key.c_str());
        } else {
          Serial.printf("[SIGNAL_DEBUG] ✗ 解析失败: 无法解析信号数据\n");
          Serial.printf("[SIGNAL_DEBUG] 数据格式可能不正确，期望格式: LC:XXXXXXYY 或 RX:XXXXXXYY\n");
        }
        
        // 清空缓冲区
        buffer = "";
        rawBytes = "";
        
        if (result) {
          signal.frequency = RF_433MHZ;
          signal.protocol = _protocol433;
          signal.pulseLength = _pulseLength433;
          
          _receiveCount++;
          
          // 添加到复刻缓冲区
          addToReplayBuffer(signal);
          
          // 检查捕获模式
          checkCaptureMode(signal);
          
          if (_receiveCallback != nullptr) {
            Serial.printf("[SIGNAL_DEBUG] 调用接收回调函数\n");
            _receiveCallback(signal);
          } else {
            Serial.printf("[SIGNAL_DEBUG] 警告: 接收回调函数未设置\n");
          }
          return true;
        }
      } else {
        // 空行，只包含换行符
        Serial.printf("[SIGNAL_DEBUG] 接收到空行 (只有换行符)\n");
        rawBytes = "";
      }
    } else {
      buffer += c;
      if (buffer.length() > 64) {
        Serial.printf("[SIGNAL_DEBUG] 警告: 缓冲区溢出，清空缓冲区 (长度=%d)\n", buffer.length());
        Serial.printf("[SIGNAL_DEBUG] 溢出前的数据: [%s]\n", buffer.c_str());
        buffer = "";  // Buffer overflow protection
        rawBytes = "";
      }
    }
  }
  
  return false;
}

// Parse signal from string
bool ESPMultiRF::parseSignal(String data, RFSignal &signal) {
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
void ESPMultiRF::send(String address, String key, RFFrequency freq) {
  _sendCount++;
  if (freq == RF_433MHZ) {
  sendSignalRCSwitch(address, key);
  } else {
    sendSignalTCSwitch(address, key);
  }
}

// Send signal (RFSignal struct)
void ESPMultiRF::send(RFSignal signal) {
  send(signal.address, signal.key, signal.frequency);
}

// Set repeat count
void ESPMultiRF::setRepeatCount(uint8_t count, RFFrequency freq) {
  if (freq == RF_433MHZ) {
    _repeatCount433 = count;
  if (_rcSwitch != nullptr) {
    _rcSwitch->setRepeatTransmit(count);
    }
  } else {
    _repeatCount315 = count;
    if (_tcSwitch != nullptr) {
      _tcSwitch->setRepeatTransmit(count);
    }
  }
}

// Set protocol
void ESPMultiRF::setProtocol(uint8_t protocol, RFFrequency freq) {
  if (freq == RF_433MHZ) {
    _protocol433 = protocol;
  if (_rcSwitch != nullptr) {
    _rcSwitch->setProtocol(protocol);
    }
  } else {
    _protocol315 = protocol;
    if (_tcSwitch != nullptr) {
      _tcSwitch->setProtocol(protocol);
    }
  }
}

// Set pulse length
void ESPMultiRF::setPulseLength(uint16_t pulseLength, RFFrequency freq) {
  if (freq == RF_433MHZ) {
    _pulseLength433 = pulseLength;
  if (_rcSwitch != nullptr) {
    _rcSwitch->setPulseLength(pulseLength);
  }
  } else {
    _pulseLength315 = pulseLength;
    if (_tcSwitch != nullptr) {
      _tcSwitch->setPulseLength(pulseLength);
    }
  }
}

// Set frequency
void ESPMultiRF::setFrequency(RFFrequency freq) {
  _currentFrequency = freq;
}

// Reset counters
void ESPMultiRF::resetCounters() {
  _sendCount = 0;
  _receiveCount = 0;
}

// Set receive callback
void ESPMultiRF::setReceiveCallback(ReceiveCallback callback) {
  _receiveCallback = callback;
}

// Convert hex character to number
uint8_t ESPMultiRF::hexToNum(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return 0;
}

// Send signal via RCSwitch
void ESPMultiRF::sendSignalRCSwitch(String address, String key) {
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
  _rcSwitch->setProtocol(_protocol433);
  _rcSwitch->setPulseLength(_pulseLength433);
  _rcSwitch->setRepeatTransmit(_repeatCount433);
  
  // 发送24位数据
  _rcSwitch->send(code24bit, 24);
  
  Serial.printf("[ESPMultiRF] 已发送24位数据 (433MHz): 0x%06lX (重复%d次)\n", code24bit, _repeatCount433);
}

// Send signal via TCSwitch (315MHz)
void ESPMultiRF::sendSignalTCSwitch(String address, String key) {
  if (_tcSwitch == nullptr) return;
  
  String fullHex = address + key;  // 例如：E7E83100 (8位十六进制)
  
  // 解析为32位数据
  uint32_t fullData = 0;
  for (int i = 0; i < 8 && i < fullHex.length(); i++) {
    uint8_t val = hexToNum(fullHex.charAt(i));
    fullData = (fullData << 4) | val;
  }
  
  // 提取24位数据：取前24位（地址码），去掉后8位（按键值）
  uint32_t code24bit = (fullData >> 8) & 0xFFFFFF;  // 前24位（地址码）
  
  // 调试输出：显示发送的数据
  Serial.printf("[ESPMultiRF] 发送数据 (315MHz) - 输入:%s%s (32位:0x%08lX), 24位:0x%06lX\n", 
               address.c_str(), key.c_str(), fullData, code24bit);
  
  // 确保TCSwitch配置正确（每次发送前检查）
  _tcSwitch->setProtocol(_protocol315);
  _tcSwitch->setPulseLength(_pulseLength315);
  _tcSwitch->setRepeatTransmit(_repeatCount315);
  
  // 发送24位数据
  _tcSwitch->send(code24bit, 24);
  
  Serial.printf("[ESPMultiRF] 已发送24位数据 (315MHz): 0x%06lX (重复%d次)\n", code24bit, _repeatCount315);
}

// ========== Replay Buffer Functions ==========

void ESPMultiRF::enableReplayBuffer(uint8_t size) {
  if (_replayBuffer != nullptr) {
    delete[] _replayBuffer;
  }
  _replayBufferSize = size;
  _replayBuffer = new RFSignal[size];
  _replayBufferIndex = 0;
  _replayBufferCount = 0;
  _replayBufferEnabled = true;
}

void ESPMultiRF::disableReplayBuffer() {
  if (_replayBuffer != nullptr) {
    delete[] _replayBuffer;
    _replayBuffer = nullptr;
  }
  _replayBufferEnabled = false;
  _replayBufferSize = 0;
  _replayBufferIndex = 0;
  _replayBufferCount = 0;
}

uint8_t ESPMultiRF::getReplayBufferCount() {
  return _replayBufferCount;
}

bool ESPMultiRF::getReplaySignal(uint8_t index, RFSignal &signal) {
  if (!_replayBufferEnabled || _replayBuffer == nullptr || index >= _replayBufferCount) {
    return false;
  }
  int startIdx = (_replayBufferIndex - _replayBufferCount + _replayBufferSize) % _replayBufferSize;
  int idx = (startIdx + index) % _replayBufferSize;
  signal = _replayBuffer[idx];
  return true;
}

RFSignal ESPMultiRF::getLastReceived() {
  return _lastReceived;
}

void ESPMultiRF::clearReplayBuffer() {
  _replayBufferIndex = 0;
  _replayBufferCount = 0;
}

void ESPMultiRF::addToReplayBuffer(RFSignal signal) {
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


void ESPMultiRF::disableCaptureMode() {
  _captureMode = false;
  Serial.printf("[ESP433RF] disableCaptureMode: 捕获模式已禁用\n");
}

bool ESPMultiRF::isCaptureMode() {
  return _captureMode;
}

bool ESPMultiRF::hasCapturedSignal() {
  return _hasCapturedSignal;
}

RFSignal ESPMultiRF::getCapturedSignal() {
  return _capturedSignal;
}

void ESPMultiRF::clearCapturedSignal() {
  _hasCapturedSignal = false;
  _capturedSignal = {"", ""};
  #ifdef ESP32
  if (_flashStorageEnabled) {
    clearFlash();
  }
  #endif
}

void ESPMultiRF::checkCaptureMode(RFSignal signal) {
  if (_captureMode) {
    Serial.printf("[ESP433RF] checkCaptureMode: 检测到捕获模式，捕获信号: %s%s\n",
                 signal.address.c_str(), signal.key.c_str());
    _capturedSignal = signal;
    _hasCapturedSignal = true;
    _captureMode = false;  // 捕获完成后自动退出捕获模式
    Serial.printf("[ESP433RF] checkCaptureMode: 信号已捕获，退出捕获模式\n");
    
    #ifdef ESP32
    if (_flashStorageEnabled) {
      saveToFlash();
    }
    #endif
  }
}

// ========== Flash Storage Functions (ESP32 only) ==========

#ifdef ESP32
void ESPMultiRF::enableFlashStorage(const char* namespace_name) {
  _flashStorageEnabled = true;
  _flashNamespace = String(namespace_name);
  if (_preferences == nullptr) {
    _preferences = new Preferences();
  }
}

void ESPMultiRF::disableFlashStorage() {
  _flashStorageEnabled = false;
  if (_preferences != nullptr) {
    delete _preferences;
    _preferences = nullptr;
  }
}

bool ESPMultiRF::saveToFlash() {
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

bool ESPMultiRF::loadFromFlash() {
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

void ESPMultiRF::clearFlash() {
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
void ESPMultiRF::enableReceive(RFFrequency freq) {
  if (freq == RF_433MHZ) {
    _receiveEnabled433 = true;
    Serial.println("[ESPMultiRF] 433MHz 接收已启用");
  } else {
    _receiveEnabled315 = true;
    if (_tcSwitch != nullptr) {
      _tcSwitch->enableReceive(_rx315Pin);
    }
    Serial.println("[ESPMultiRF] 315MHz 接收已启用");
  }
}

void ESPMultiRF::disableReceive(RFFrequency freq) {
  if (freq == RF_433MHZ) {
    _receiveEnabled433 = false;
    Serial.println("[ESPMultiRF] 433MHz 接收已禁用");
  } else {
    _receiveEnabled315 = false;
    if (_tcSwitch != nullptr) {
      _tcSwitch->disableReceive();
    }
    Serial.println("[ESPMultiRF] 315MHz 接收已禁用");
  }
}

bool ESPMultiRF::isReceiving(RFFrequency freq) {
  if (freq == RF_433MHZ) {
    return _receiveEnabled433;
  } else {
    return _receiveEnabled315;
  }
}

// ========== Raw Signal Functions ==========

// 接收原始信号（用于无法解码的情况）
bool ESPMultiRF::receiveRaw(RFRawSignal &rawSignal, RFFrequency freq) {
  if (freq == RF_433MHZ) {
    // 433MHz 使用 RCSwitch 的原始数据
    if (_rcSwitch != nullptr && _rcSwitch->available()) {
      unsigned int* timings = _rcSwitch->getReceivedRawdata();
      if (timings != nullptr) {
        // 需要知道变化次数，但 RCSwitch 没有提供，我们使用最大值
        rawSignal.timings = new unsigned int[RCSWITCH_MAX_CHANGES];
        for (int i = 0; i < RCSWITCH_MAX_CHANGES; i++) {
          rawSignal.timings[i] = timings[i];
        }
        rawSignal.changeCount = RCSWITCH_MAX_CHANGES;
        rawSignal.frequency = RF_433MHZ;
        rawSignal.isValid = true;
        _rcSwitch->resetAvailable();
        return true;
      }
      _rcSwitch->resetAvailable();
    }
  } else {
    // 315MHz 使用 TCSwitch 的原始数据
    if (_tcSwitch != nullptr && _tcSwitch->available()) {
      unsigned int* timings = _tcSwitch->getReceivedRawdata();
      unsigned int changeCount = _tcSwitch->getReceivedChangeCount();
      if (timings != nullptr && changeCount > 0) {
        rawSignal.timings = new unsigned int[changeCount];
        for (unsigned int i = 0; i < changeCount; i++) {
          rawSignal.timings[i] = timings[i];
        }
        rawSignal.changeCount = changeCount;
        rawSignal.frequency = RF_315MHZ;
        rawSignal.isValid = true;
        _tcSwitch->resetAvailable();
        return true;
      }
      _tcSwitch->resetAvailable();
    }
  }
  rawSignal.isValid = false;
  return false;
}

// 发送原始信号（用于无法解码的情况）
void ESPMultiRF::sendRaw(RFRawSignal &rawSignal, unsigned int repeatCount) {
  if (!rawSignal.isValid || rawSignal.timings == nullptr || rawSignal.changeCount == 0) {
    Serial.println("[ESPMultiRF] 警告: 原始信号无效，无法发送");
    return;
  }
  
  _sendCount++;
  
  if (rawSignal.frequency == RF_433MHZ) {
    // 433MHz 使用 RCSwitch
    if (_rcSwitch != nullptr) {
      // 注意：RCSwitch 没有 sendRaw 方法，我们需要自己实现
      // 这里我们创建一个包装方法
      Serial.println("[ESPMultiRF] 警告: RCSwitch 不支持原始信号发送，需要扩展实现");
    }
  } else {
    // 315MHz 使用 TCSwitch
    if (_tcSwitch != nullptr) {
      _tcSwitch->sendRaw(rawSignal.timings, rawSignal.changeCount, repeatCount);
      Serial.printf("[ESPMultiRF] 已发送原始信号 (315MHz): %d 个时序变化 (重复%d次)\n", 
                   rawSignal.changeCount, repeatCount);
    }
  }
}

// 设置原始信号接收回调
void ESPMultiRF::setRawReceiveCallback(RawReceiveCallback callback) {
  _rawReceiveCallback = callback;
}

// ========== Raw Replay Buffer Functions ==========

void ESPMultiRF::enableRawReplayBuffer(uint8_t size) {
  if (_rawReplayBuffer != nullptr) {
    clearRawReplayBuffer();
  }
  _rawReplayBufferSize = size;
  _rawReplayBuffer = new RFRawSignal[size];
  _rawReplayBufferTimings = new unsigned int*[size];
  for (uint8_t i = 0; i < size; i++) {
    _rawReplayBuffer[i].timings = nullptr;
    _rawReplayBuffer[i].isValid = false;
    _rawReplayBufferTimings[i] = nullptr;
  }
  _rawReplayBufferIndex = 0;
  _rawReplayBufferCount = 0;
  _rawReplayBufferEnabled = true;
}

void ESPMultiRF::disableRawReplayBuffer() {
  clearRawReplayBuffer();
  _rawReplayBufferEnabled = false;
  _rawReplayBufferSize = 0;
  _rawReplayBufferIndex = 0;
  _rawReplayBufferCount = 0;
}

uint8_t ESPMultiRF::getRawReplayBufferCount() {
  return _rawReplayBufferCount;
}

bool ESPMultiRF::getRawReplaySignal(uint8_t index, RFRawSignal &rawSignal) {
  if (!_rawReplayBufferEnabled || _rawReplayBuffer == nullptr || index >= _rawReplayBufferCount) {
    return false;
  }
  int startIdx = (_rawReplayBufferIndex - _rawReplayBufferCount + _rawReplayBufferSize) % _rawReplayBufferSize;
  int idx = (startIdx + index) % _rawReplayBufferSize;
  rawSignal = copyRawSignal(_rawReplayBuffer[idx]);
  return true;
}

RFRawSignal ESPMultiRF::getLastReceivedRaw() {
  return copyRawSignal(_lastReceivedRaw);
}

void ESPMultiRF::clearRawReplayBuffer() {
  if (_rawReplayBuffer != nullptr) {
    for (uint8_t i = 0; i < _rawReplayBufferSize; i++) {
      if (_rawReplayBuffer[i].timings != nullptr) {
        delete[] _rawReplayBuffer[i].timings;
        _rawReplayBuffer[i].timings = nullptr;
      }
      if (_rawReplayBufferTimings[i] != nullptr) {
        delete[] _rawReplayBufferTimings[i];
        _rawReplayBufferTimings[i] = nullptr;
      }
    }
    delete[] _rawReplayBuffer;
    _rawReplayBuffer = nullptr;
  }
  if (_rawReplayBufferTimings != nullptr) {
    delete[] _rawReplayBufferTimings;
    _rawReplayBufferTimings = nullptr;
  }
  _rawReplayBufferSize = 0;
  _rawReplayBufferIndex = 0;
  _rawReplayBufferCount = 0;
}

void ESPMultiRF::addToRawReplayBuffer(RFRawSignal rawSignal) {
  if (!_rawReplayBufferEnabled || _rawReplayBuffer == nullptr) {
    return;
  }
  
  // 复制原始信号
  RFRawSignal copied = copyRawSignal(rawSignal);
  
  // 保存到缓冲区
  if (_rawReplayBuffer[_rawReplayBufferIndex].timings != nullptr) {
    delete[] _rawReplayBuffer[_rawReplayBufferIndex].timings;
  }
  _rawReplayBuffer[_rawReplayBufferIndex] = copied;
  
  _rawReplayBufferIndex = (_rawReplayBufferIndex + 1) % _rawReplayBufferSize;
  if (_rawReplayBufferCount < _rawReplayBufferSize) {
    _rawReplayBufferCount++;
  }
  
  _lastReceivedRaw = copyRawSignal(copied);
}

// ========== Raw Capture Mode Functions ==========

void ESPMultiRF::enableCaptureMode(bool rawMode) {
  if (rawMode) {
    _rawCaptureMode = true;
    _captureMode = false;
    Serial.println("[ESPMultiRF] 原始信号捕获模式已启用");
  } else {
    _captureMode = true;
    _rawCaptureMode = false;
    Serial.println("[ESPMultiRF] 信号捕获模式已启用");
  }
}

bool ESPMultiRF::isRawCaptureMode() {
  return _rawCaptureMode;
}

bool ESPMultiRF::hasCapturedRawSignal() {
  return _hasCapturedRawSignal;
}

RFRawSignal ESPMultiRF::getCapturedRawSignal() {
  return copyRawSignal(_capturedRawSignal);
}

void ESPMultiRF::checkRawCaptureMode(RFRawSignal rawSignal) {
  if (_rawCaptureMode && rawSignal.isValid) {
    freeRawSignal(_capturedRawSignal);
    _capturedRawSignal = copyRawSignal(rawSignal);
    _hasCapturedRawSignal = true;
    _rawCaptureMode = false;
    Serial.println("[ESPMultiRF] 原始信号已捕获");
    
    // 添加到原始信号缓冲区
    addToRawReplayBuffer(rawSignal);
    
    // 调用回调
    if (_rawReceiveCallback != nullptr) {
      _rawReceiveCallback(copyRawSignal(rawSignal));
    }
  }
}

// ========== Helper Functions ==========

void ESPMultiRF::freeRawSignal(RFRawSignal &rawSignal) {
  if (rawSignal.timings != nullptr) {
    delete[] rawSignal.timings;
    rawSignal.timings = nullptr;
  }
  rawSignal.isValid = false;
  rawSignal.changeCount = 0;
}

RFRawSignal ESPMultiRF::copyRawSignal(RFRawSignal &src) {
  RFRawSignal dst;
  dst.frequency = src.frequency;
  dst.isValid = src.isValid;
  dst.changeCount = src.changeCount;
  
  if (src.isValid && src.timings != nullptr && src.changeCount > 0) {
    dst.timings = new unsigned int[src.changeCount];
    for (unsigned int i = 0; i < src.changeCount; i++) {
      dst.timings[i] = src.timings[i];
    }
  } else {
    dst.timings = nullptr;
  }
  
  return dst;
}
