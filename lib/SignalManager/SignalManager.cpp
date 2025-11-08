/*
 * SignalManager - 433MHz信号管理库实现
 */

#include "SignalManager.h"

SignalManager::SignalManager(uint8_t maxSignals) {
  _maxSignals = maxSignals;
  _signals = nullptr;
  _count = 0;
  
  #ifdef ESP32
  _preferences = nullptr;
  _flashEnabled = false;
  _flashNamespace = "signal_mgr";
  #endif
}

SignalManager::~SignalManager() {
  end();
}

void SignalManager::begin() {
  if (_signals == nullptr) {
    _signals = new SignalItem[_maxSignals];
    _count = 0;
  }
  
  #ifdef ESP32
  initFlash();
  loadFromFlash();
  #endif
}

void SignalManager::end() {
  if (_signals != nullptr) {
    #ifdef ESP32
    saveToFlash();
    #endif
    delete[] _signals;
    _signals = nullptr;
  }
  _count = 0;
  
  #ifdef ESP32
  if (_preferences != nullptr) {
    delete _preferences;
    _preferences = nullptr;
  }
  #endif
}

bool SignalManager::addSignal(const String& name, const RFSignal& signal) {
  if (_signals == nullptr || _count >= _maxSignals) {
    return false;
  }
  
  // 检查名称是否已存在
  for (uint8_t i = 0; i < _count; i++) {
    if (_signals[i].name == name) {
      // 更新现有信号
      _signals[i].signal = signal;
      _signals[i].timestamp = millis();
      return true;
    }
  }
  
  // 添加新信号
  _signals[_count].name = name;
  _signals[_count].signal = signal;
  _signals[_count].timestamp = millis();
  _count++;
  
  #ifdef ESP32
  if (_flashEnabled) {
    saveToFlash();
  }
  #endif
  
  return true;
}

bool SignalManager::addSignal(const RFSignal& signal) {
  String name = generateAutoName(_count);
  return addSignal(name, signal);
}

bool SignalManager::removeSignal(uint8_t index) {
  if (_signals == nullptr || index >= _count) {
    return false;
  }
  
  // 移动后续元素
  for (uint8_t i = index; i < _count - 1; i++) {
    _signals[i] = _signals[i + 1];
  }
  _count--;
  
  #ifdef ESP32
  if (_flashEnabled) {
    saveToFlash();
  }
  #endif
  
  return true;
}

bool SignalManager::removeSignal(const String& name) {
  if (_signals == nullptr) {
    return false;
  }
  
  for (uint8_t i = 0; i < _count; i++) {
    if (_signals[i].name == name) {
      return removeSignal(i);
    }
  }
  
  return false;
}

bool SignalManager::updateSignal(uint8_t index, const String& name, const RFSignal& signal) {
  if (_signals == nullptr || index >= _count) {
    return false;
  }
  
  _signals[index].name = name;
  _signals[index].signal = signal;
  _signals[index].timestamp = millis();
  
  #ifdef ESP32
  if (_flashEnabled) {
    saveToFlash();
  }
  #endif
  
  return true;
}

bool SignalManager::getSignal(uint8_t index, SignalItem& item) {
  if (_signals == nullptr || index >= _count) {
    return false;
  }
  
  item = _signals[index];
  return true;
}

bool SignalManager::getSignal(const String& name, SignalItem& item) {
  if (_signals == nullptr) {
    return false;
  }
  
  for (uint8_t i = 0; i < _count; i++) {
    if (_signals[i].name == name) {
      item = _signals[i];
      return true;
    }
  }
  
  return false;
}

uint8_t SignalManager::getCount() {
  return _count;
}

void SignalManager::clear() {
  _count = 0;
  
  #ifdef ESP32
  if (_flashEnabled) {
    clearFlash();
  }
  #endif
}

bool SignalManager::sendSignal(uint8_t index, ESP433RF& rf) {
  if (_signals == nullptr || index >= _count) {
    return false;
  }
  
  // 发送前临时禁用接收，避免接收到自己发送的信号
  bool wasReceiving = rf.isReceiving();
  if (wasReceiving) {
    rf.disableReceive();
  }
  
  rf.send(_signals[index].signal);
  
  // 延迟一下，确保发送完成
  delay(200);
  
  // 恢复接收
  if (wasReceiving) {
    rf.enableReceive();
  }
  
  return true;
}

bool SignalManager::sendSignal(const String& name, ESP433RF& rf) {
  if (_signals == nullptr) {
    return false;
  }
  
  for (uint8_t i = 0; i < _count; i++) {
    if (_signals[i].name == name) {
      // 发送前临时禁用接收，避免接收到自己发送的信号
      bool wasReceiving = rf.isReceiving();
      if (wasReceiving) {
        rf.disableReceive();
      }
      
      rf.send(_signals[i].signal);
      
      // 延迟一下，确保发送完成
      delay(200);
      
      // 恢复接收
      if (wasReceiving) {
        rf.enableReceive();
      }
      
      return true;
    }
  }
  
  return false;
}

bool SignalManager::getAllSignals(SignalItem* items, uint8_t maxCount) {
  if (_signals == nullptr || items == nullptr) {
    return false;
  }
  
  uint8_t copyCount = (_count < maxCount) ? _count : maxCount;
  for (uint8_t i = 0; i < copyCount; i++) {
    items[i] = _signals[i];
  }
  
  return true;
}

String SignalManager::generateAutoName(uint8_t index) {
  return "Signal_" + String(index + 1);
}

#ifdef ESP32
void SignalManager::initFlash() {
  if (_preferences == nullptr) {
    _preferences = new Preferences();
  }
  _flashEnabled = true;
}

bool SignalManager::saveToFlash() {
  if (!_flashEnabled || _preferences == nullptr || _signals == nullptr) {
    return false;
  }
  
  _preferences->begin(_flashNamespace.c_str(), false);
  _preferences->putUChar("count", _count);
  
  for (uint8_t i = 0; i < _count; i++) {
    String keyPrefix = "sig_" + String(i) + "_";
    _preferences->putString((keyPrefix + "name").c_str(), _signals[i].name);
    _preferences->putString((keyPrefix + "addr").c_str(), _signals[i].signal.address);
    _preferences->putString((keyPrefix + "key").c_str(), _signals[i].signal.key);
    _preferences->putULong((keyPrefix + "time").c_str(), _signals[i].timestamp);
  }
  
  _preferences->end();
  return true;
}

bool SignalManager::loadFromFlash() {
  if (!_flashEnabled || _preferences == nullptr || _signals == nullptr) {
    return false;
  }
  
  _preferences->begin(_flashNamespace.c_str(), true);
  uint8_t savedCount = _preferences->getUChar("count", 0);
  
  if (savedCount > _maxSignals) {
    savedCount = _maxSignals;
  }
  
  _count = 0;
  for (uint8_t i = 0; i < savedCount; i++) {
    String keyPrefix = "sig_" + String(i) + "_";
    String name = _preferences->getString((keyPrefix + "name").c_str(), "");
    String addr = _preferences->getString((keyPrefix + "addr").c_str(), "");
    String key = _preferences->getString((keyPrefix + "key").c_str(), "");
    
    if (name.length() > 0 && addr.length() > 0 && key.length() > 0) {
      _signals[_count].name = name;
      _signals[_count].signal.address = addr;
      _signals[_count].signal.key = key;
      _signals[_count].timestamp = _preferences->getULong((keyPrefix + "time").c_str(), millis());
      _count++;
    }
  }
  
  _preferences->end();
  return true;
}

void SignalManager::clearFlash() {
  if (!_flashEnabled || _preferences == nullptr) {
    return;
  }
  
  _preferences->begin(_flashNamespace.c_str(), false);
  _preferences->clear();
  _preferences->end();
}
#endif

