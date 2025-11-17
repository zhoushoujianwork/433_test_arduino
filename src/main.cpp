#include <Arduino.h>
// 启用RCSwitch库支持
#define USE_RCSWITCH 1
#include <ESPMultiRF.h>
#include <Preferences.h>  // ESP32闪存存储库
#include <SignalManager.h>  // 信号管理库
#include <ESPMultiRFWeb.h>    // Web管理界面库

// 调试开关 - 设置为1启用详细调试信息，0禁用
#define DEBUG_CAPTURE_MODE 1

// 硬件引脚定义
#define TX_PIN 14       // 发射模块DATA引脚
#define RX_PIN 18       // 接收模块数据引脚
#define REPLAY_BUTTON_PIN 0  // 复刻按钮GPIO引脚（绑定到boot按键，按下时发送复刻信号）
#define LED_PIN 21     // LED指示灯引脚

// 当前发送的信号（用于验证，通过串口命令发送时记录）
RFSignal currentSent = {"", ""};

// 复刻功能：保存接收到的信号
#define REPLAY_BUFFER_SIZE 10
RFSignal replayBuffer[REPLAY_BUFFER_SIZE];
int replayBufferIndex = 0;
int replayBufferCount = 0;
RFSignal lastReceived = {"", ""};  // 最后接收到的信号

// 复刻模式状态
bool replayMode = false;           // 是否处于复刻模式（等待接收信号）
RFSignal capturedSignal = {"", ""}; // 捕获的信号（用于GPIO触发发送）
bool signalCaptured = false;       // 是否已捕获信号

// LED状态管理
enum LEDState {
  LED_OFF,      // 熄灭（没有复刻信号）
  LED_BLINK,    // 快闪（复刻状态，等待接收信号）
  LED_ON        // 常亮（完成复刻，已捕获信号）
};
LEDState currentLEDState = LED_OFF;  // 当前LED状态

// 全局变量
static uint32_t sendCount = 0;
static uint32_t receiveCount = 0;
static bool testPassed = false;

// 创建ESPMultiRF实例（支持433MHz和315MHz）
// 参数：TX433, RX433, TX315, RX315, 波特率
ESPMultiRF rf(TX_PIN, RX_PIN, 15, 19, 9600);  // TODO: 更新315MHz引脚

// 创建信号管理器实例（最多50个信号）
SignalManager signalManager(50);

// 创建Web管理界面实例
ESPMultiRFWeb webManager(rf, signalManager);

// 闪存存储实例（保留用于向后兼容）
Preferences preferences;
const char* PREF_NAMESPACE = "rf_replay";  // 命名空间
const char* PREF_KEY_ADDRESS = "address";  // 地址码键
const char* PREF_KEY_KEY = "key";          // 按键值键
const char* PREF_KEY_CAPTURED = "captured"; // 是否已捕获标志

// 保存信号到闪存
void saveSignalToFlash() {
  preferences.begin(PREF_NAMESPACE, false);  // false表示读写模式
  if (signalCaptured && capturedSignal.address.length() > 0) {
    preferences.putString(PREF_KEY_ADDRESS, capturedSignal.address);
    preferences.putString(PREF_KEY_KEY, capturedSignal.key);
    preferences.putBool(PREF_KEY_CAPTURED, true);
    Serial.println("[FLASH] 信号已保存到闪存");
  } else {
    // 清空闪存
    preferences.remove(PREF_KEY_ADDRESS);
    preferences.remove(PREF_KEY_KEY);
    preferences.putBool(PREF_KEY_CAPTURED, false);
    Serial.println("[FLASH] 闪存已清空");
  }
  preferences.end();
}

// 从闪存加载信号
void loadSignalFromFlash() {
  preferences.begin(PREF_NAMESPACE, true);  // true表示只读模式
  bool saved = preferences.getBool(PREF_KEY_CAPTURED, false);
  if (saved) {
    capturedSignal.address = preferences.getString(PREF_KEY_ADDRESS, "");
    capturedSignal.key = preferences.getString(PREF_KEY_KEY, "");
    if (capturedSignal.address.length() > 0 && capturedSignal.key.length() > 0) {
      signalCaptured = true;
      currentLEDState = LED_ON;  // 已加载信号，LED常亮
      Serial.printf("[FLASH] 从闪存加载信号: %s%s\n", 
                   capturedSignal.address.c_str(), capturedSignal.key.c_str());
    } else {
      signalCaptured = false;
      Serial.println("[FLASH] 闪存中的信号数据无效");
    }
  } else {
    signalCaptured = false;
    Serial.println("[FLASH] 闪存中没有保存的信号");
  }
  preferences.end();
}

// 接收回调函数
void onReceive(RFSignal signal) {
  receiveCount++;
  Serial.println("========================================");
  Serial.printf("[RECV] 第%lu次接收信号\n", receiveCount);
  Serial.printf("[RECV] 地址码: %s (长度: %d)\n", signal.address.c_str(), signal.address.length());
  Serial.printf("[RECV] 按键值: %s (长度: %d)\n", signal.key.c_str(), signal.key.length());
  Serial.printf("[RECV] 完整数据: %s%s\n", signal.address.c_str(), signal.key.c_str());
  
  // 显示十六进制表示
  String fullHex = signal.address + signal.key;
  Serial.printf("[RECV] 十六进制: ");
  for (int i = 0; i < fullHex.length(); i++) {
    char c = fullHex.charAt(i);
    Serial.printf("%c", c);
    if ((i + 1) % 2 == 0 && i < fullHex.length() - 1) {
      Serial.printf(" ");
    }
  }
  Serial.println();
  
  // 计算数值
  uint32_t fullData = 0;
  for (int i = 0; i < fullHex.length() && i < 8; i++) {
    char c = fullHex.charAt(i);
    uint8_t val = 0;
    if (c >= '0' && c <= '9') val = c - '0';
    else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;
    else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
    fullData = (fullData << 4) | val;
  }
  Serial.printf("[RECV] 数值表示: 32位=0x%08lX, 十进制=%lu\n", fullData, fullData);
  Serial.println("========================================");
  
  // 保存接收到的信号到复刻缓冲区（向后兼容）
  lastReceived = signal;
  replayBuffer[replayBufferIndex] = signal;
  replayBufferIndex = (replayBufferIndex + 1) % REPLAY_BUFFER_SIZE;
  if (replayBufferCount < REPLAY_BUFFER_SIZE) {
    replayBufferCount++;
  }
  
  // 只在捕获模式下添加到信号管理器
  #if DEBUG_CAPTURE_MODE
  Serial.printf("[DEBUG] onReceive: 检查捕获模式 - replayMode=%d, rf.isCaptureMode()=%d\n", 
               replayMode, rf.isCaptureMode());
  #endif
  
  if (replayMode || rf.isCaptureMode()) {
    #if DEBUG_CAPTURE_MODE
    Serial.printf("[DEBUG] onReceive: 进入捕获处理逻辑\n");
    #endif
    
    // 去重：检查是否已存在相同的信号
    bool isDuplicate = false;
    uint8_t count = signalManager.getCount();
    SignalItem item;
    for (uint8_t i = 0; i < count; i++) {
      if (signalManager.getSignal(i, item)) {
        if (item.signal.address == signal.address && item.signal.key == signal.key) {
          isDuplicate = true;
          Serial.printf("[SIGNAL_MGR] 信号已存在，跳过: %s%s\n", 
                       signal.address.c_str(), signal.key.c_str());
          break;
        }
      }
    }
    
    // 只有不重复的信号才添加
    if (!isDuplicate) {
      // 生成自动名称
      String autoName = "Signal_" + String(signalManager.getCount() + 1);
      signalManager.addSignal(autoName, signal);
      Serial.printf("[SIGNAL_MGR] 信号已添加到管理器: %s (%s%s)\n", 
                   autoName.c_str(), signal.address.c_str(), signal.key.c_str());
    }
    
    // 捕获一个信号后自动退出捕获模式
    capturedSignal = signal;  // 保存完整的信号（地址码+按键值）
    signalCaptured = true;
    replayMode = false;  // 捕获完成后退出复刻模式
    currentLEDState = LED_ON;  // 完成复刻，LED常亮
    rf.disableCaptureMode();  // 禁用库的捕获模式
    Serial.println("[CAPTURE] 已退出捕获模式");
    
    #if DEBUG_CAPTURE_MODE
    Serial.printf("[DEBUG] onReceive: 捕获完成 - signalCaptured=%d, replayMode=%d, isCaptureMode=%d\n",
                 signalCaptured, replayMode, rf.isCaptureMode());
    #endif
    
    // 保存到闪存（向后兼容）
    saveSignalToFlash();
    
    // 计算实际发送的24位数据（前24位，去掉最后8位）
    String fullHex = capturedSignal.address + capturedSignal.key;
    uint32_t fullData = 0;
    for (int i = 0; i < 8 && i < fullHex.length(); i++) {
      char c = fullHex.charAt(i);
      uint8_t val = 0;
      if (c >= '0' && c <= '9') val = c - '0';
      else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;
      else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
      fullData = (fullData << 4) | val;
    }
    uint32_t code24bit = (fullData >> 8) & 0xFFFFFF;  // 前24位（去掉最后8位）
    
    Serial.printf("[REPLAY] ✓ 信号已捕获: %s%s (地址码:%s, 按键值:%s)\n", 
                 capturedSignal.address.c_str(), capturedSignal.key.c_str(),
                 capturedSignal.address.c_str(), capturedSignal.key.c_str());
    Serial.printf("[REPLAY] 实际将发送: 32位=0x%08lX, 24位=0x%06lX\n", fullData, code24bit);
    Serial.printf("[REPLAY] 现在可以按下GPIO%d按钮发送复刻信号\n", REPLAY_BUTTON_PIN);
    Serial.printf("[REPLAY] 提示：复刻时将发送完整的8位数据 %s%s（24位编码）\n",
                 capturedSignal.address.c_str(), capturedSignal.key.c_str());
  }
  
  // 如果有发送记录，进行验证
  if (currentSent.address.length() > 0) {
    // 计算发送的实际24位数据（RCSwitch发送的是前24位，去掉最后8位）
    String fullHex = currentSent.address + currentSent.key;
    uint32_t sentFullData = 0;
    for (int i = 0; i < 8 && i < fullHex.length(); i++) {
      char c = fullHex.charAt(i);
      uint8_t val = 0;
      if (c >= '0' && c <= '9') val = c - '0';
      else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;
      else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
      sentFullData = (sentFullData << 4) | val;
    }
    uint32_t sent24bit = (sentFullData >> 8) & 0xFFFFFF;  // 前24位（去掉最后8位）
    
    // 将接收到的地址码转换为数值
    uint32_t recvAddress = 0;
    for (int i = 0; i < signal.address.length() && i < 6; i++) {
      char c = signal.address.charAt(i);
      uint8_t val = 0;
      if (c >= '0' && c <= '9') val = c - '0';
      else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;
      else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
      recvAddress = (recvAddress << 4) | val;
    }
    
    // 验证：只比较地址码（前6位），忽略按键值
    char sentHex[7];
    sprintf(sentHex, "%06lX", sent24bit);
    bool matchAddress = (signal.address == String(sentHex));
    
    if (matchAddress) {
      testPassed = true;
      Serial.printf("[TEST] ✓ 验证通过！地址码匹配\n");
      Serial.printf("[TEST]   期望地址码:%s (24位:0x%06lX), 接收地址码:%s (按键:%s)\n", 
                  sentHex, sent24bit, signal.address.c_str(), signal.key.c_str());
    } else {
      Serial.printf("[TEST] ✗ 验证失败！\n");
      Serial.printf("[TEST]   期望地址码:%s (24位:0x%06lX)\n", 
                  sentHex, sent24bit);
      Serial.printf("[TEST]   接收地址码:%s (按键:%s, 地址码:0x%06lX)\n", 
                  signal.address.c_str(), signal.key.c_str(), recvAddress);
    }
  }
}

// 接收任务
void receiveTask(void *parameter) {
  while (true) {
    // 检查接收（回调函数会自动处理）
    int availableBytes = Serial1.available();
    
    if (availableBytes > 0) {
      #if DEBUG_CAPTURE_MODE
      static unsigned long lastDebugTime = 0;
      if (millis() - lastDebugTime > 1000) {  // 每秒输出一次状态
        Serial.printf("[DEBUG] receiveTask: 串口有数据可用, available=%d 字节\n", availableBytes);
        Serial.printf("[DEBUG] receiveTask: replayMode=%d, isCaptureMode=%d, receiveEnabled=%d\n", 
                     replayMode, rf.isCaptureMode(), rf.isReceiving());
        lastDebugTime = millis();
      }
      #endif
      
      RFSignal signal;
      if (rf.receive(signal)) {
        #if DEBUG_CAPTURE_MODE
        Serial.printf("[DEBUG] receiveTask: ✓ 成功接收到信号\n");
        #endif
        // 回调函数已经处理了验证逻辑
      } else {
        #if DEBUG_CAPTURE_MODE
        Serial.printf("[DEBUG] receiveTask: ✗ receive()返回false，可能解析失败\n");
        #endif
      }
    } else {
      #if DEBUG_CAPTURE_MODE
      static unsigned long lastNoDataTime = 0;
      static unsigned long lastNoDataLog = 0;
      if (millis() - lastNoDataTime > 5000) {  // 每5秒输出一次
        if (millis() - lastNoDataLog > 5000) {
          Serial.printf("[DEBUG] receiveTask: 串口无数据, available=%d\n", availableBytes);
          Serial.printf("[DEBUG] receiveTask: 当前状态 - replayMode=%d, isCaptureMode=%d\n", 
                       replayMode, rf.isCaptureMode());
          Serial.printf("[DEBUG] receiveTask: 接收引脚 GPIO%d, 波特率 9600\n", RX_PIN);
          lastNoDataLog = millis();
        }
        lastNoDataTime = millis();
      }
      #endif
    }
    
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// 状态监控任务
void statusTask(void *parameter) {
  while (true) {
    Serial.printf("[STATUS] 发送:%lu次, 接收:%lu次, 测试:%s\n", 
                  sendCount, receiveCount, testPassed ? "通过" : "进行中");
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

// LED控制任务 - 根据复刻状态控制LED（反向逻辑）
void ledTask(void *parameter) {
  unsigned long lastBlinkTime = 0;
  bool ledBlinkState = false;
  const unsigned long blinkInterval = 200;  // 快闪间隔200ms
  
  while (true) {
    switch (currentLEDState) {
      case LED_OFF:
        digitalWrite(LED_PIN, HIGH);  // 熄灭（反向：HIGH熄灭）
        break;
        
      case LED_BLINK:
        // 快闪：每200ms切换一次（反向逻辑）
        if (millis() - lastBlinkTime >= blinkInterval) {
          ledBlinkState = !ledBlinkState;
          digitalWrite(LED_PIN, ledBlinkState ? LOW : HIGH);  // 反向：LOW亮，HIGH灭
          lastBlinkTime = millis();
        }
        break;
        
      case LED_ON:
        digitalWrite(LED_PIN, LOW);  // 常亮（反向：LOW常亮）
        break;
    }
    
    vTaskDelay(pdMS_TO_TICKS(10));  // 10ms更新间隔
  }
}

// GPIO按钮检测任务 - 检测复刻按钮按下（支持短按和长按）
void buttonTask(void *parameter) {
  bool lastStableState = HIGH;
  bool currentReading = HIGH;
  bool lastReading = HIGH;
  unsigned long lastDebounceTime = 0;
  const unsigned long debounceDelay = 50;  // 防抖延迟50ms
  unsigned long buttonPressStartTime = 0;
  const unsigned long longPressDuration = 2000;  // 长按时间2秒
  bool buttonPressed = false;  // 防止重复触发
  bool longPressTriggered = false;  // 长按已触发标志
  
  while (true) {
    currentReading = digitalRead(REPLAY_BUTTON_PIN);
    
    // 检测状态变化
    if (currentReading != lastReading) {
      // 状态发生变化，重置防抖计时器
      lastDebounceTime = millis();
    }
    
    // 如果状态稳定超过防抖时间
    if ((millis() - lastDebounceTime) > debounceDelay) {
      // 状态已稳定
      if (currentReading != lastStableState) {
        // 稳定状态发生变化
        if (currentReading == LOW && lastStableState == HIGH) {
          // 从HIGH稳定变为LOW（按下）
          if (!buttonPressed) {
            buttonPressed = true;
            buttonPressStartTime = millis();
            longPressTriggered = false;
            Serial.printf("\n[BUTTON] ✓ 检测到按钮按下（GPIO%d）\n", REPLAY_BUTTON_PIN);
          }
        } else if (currentReading == HIGH && lastStableState == LOW) {
          // 从LOW稳定变为HIGH（释放）
          if (buttonPressed) {
            unsigned long pressDuration = millis() - buttonPressStartTime;
            
            if (!longPressTriggered && pressDuration < longPressDuration) {
              // 短按：优先发送Web绑定的信号，否则发送复刻信号
              Serial.printf("[BUTTON] 短按检测（%lums）\n", pressDuration);
              
              // 检查是否有Web绑定的信号
              int8_t boundIndex = webManager.getBootBoundIndex();
              if (boundIndex >= 0) {
                // 发送Web绑定的信号
                Serial.printf("[BUTTON] 发送Web绑定信号 #%d\n", boundIndex);
                if (signalManager.sendSignal(boundIndex, rf)) {
                  Serial.println("[BUTTON] Web绑定信号已发送");
                  sendCount++;
                } else {
                  Serial.println("[BUTTON] 警告：Web绑定信号发送失败");
                }
              } else if (signalCaptured) {
                // 发送复刻信号
                currentSent = capturedSignal;  // 记录发送的信号用于验证
                Serial.printf("[REPLAY] 发送复刻信号: %s%s\n", 
                             capturedSignal.address.c_str(), capturedSignal.key.c_str());
                Serial.printf("[REPLAY] 地址码: %s, 按键值: %s\n",
                             capturedSignal.address.c_str(), capturedSignal.key.c_str());
                
                // 计算实际发送的24位数据（前24位，去掉最后8位）
                String fullHex = capturedSignal.address + capturedSignal.key;
                uint32_t fullData = 0;
                for (int i = 0; i < 8 && i < fullHex.length(); i++) {
                  char c = fullHex.charAt(i);
                  uint8_t val = 0;
                  if (c >= '0' && c <= '9') val = c - '0';
                  else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;
                  else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
                  fullData = (fullData << 4) | val;
                }
                uint32_t code24bit = (fullData >> 8) & 0xFFFFFF;  // 前24位（去掉最后8位）
                Serial.printf("[REPLAY] 实际发送: 32位=0x%08lX, 24位=0x%06lX\n", fullData, code24bit);
                
                rf.send(capturedSignal);  // 发送完整信号（地址码+按键值）
                sendCount++;
              } else {
                Serial.println("[BUTTON] 警告：没有绑定或捕获的信号");
                Serial.println("[BUTTON] 提示：在Web界面绑定信号或使用 'capture' 命令捕获信号");
              }
            } else if (longPressTriggered) {
              Serial.println("[BUTTON] 长按释放：复刻信号已清空");
            }
            
            buttonPressed = false;
            Serial.printf("[BUTTON] 按钮释放（GPIO%d断开）\n", REPLAY_BUTTON_PIN);
          }
        }
        lastStableState = currentReading;
      }
      
      // 持续检测长按（按钮持续按下时）
      if (buttonPressed && currentReading == LOW && !longPressTriggered) {
        unsigned long pressDuration = millis() - buttonPressStartTime;
        
        // 检测长按（按下超过2秒）- 立即清空，不等待释放
        if (pressDuration >= longPressDuration) {
          longPressTriggered = true;
          Serial.println("[BUTTON] 长按检测（2秒）：立即清空复刻信号");
          
          // 立即清空复刻信号
          signalCaptured = false;
          capturedSignal = {"", ""};
          replayMode = true;  // 清空后自动进入复刻模式
          currentLEDState = LED_BLINK;  // LED快闪，等待接收信号
          
          // 清空闪存
          saveSignalToFlash();
          
          Serial.println("[REPLAY] 复刻信号已清空（内存+闪存），自动进入复刻模式");
        } else {
          // 显示长按倒计时（可选，每500ms显示一次）
          static unsigned long lastProgressTime = 0;
          if (millis() - lastProgressTime >= 500) {
            unsigned long remaining = longPressDuration - pressDuration;
            Serial.printf("[BUTTON] 长按中... 还需按住 %lums 才能清空\n", remaining);
            lastProgressTime = millis();
          }
        }
      }
    }
    
    lastReading = currentReading;
    vTaskDelay(pdMS_TO_TICKS(10));  // 10ms检测间隔
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("========================================");
  Serial.println("ESP32 433MHz 收发测试 (使用ESPMultiRF库)");
  Serial.println("========================================");
  
  // 初始化ESPMultiRF库（仅支持RCSwitch模式）
  rf.begin();
  
  // 配置库参数
  rf.setRepeatCount(5);     // 重复5次
  rf.setProtocol(1);        // Protocol 1 (EV1527/PT2262)
  rf.setPulseLength(320);  // 320μs脉冲长度
  
  // 设置接收回调
  rf.setReceiveCallback(onReceive);
  
  // 初始化信号管理器
  signalManager.begin();
  Serial.println("[SIGNAL_MGR] 信号管理器已初始化");
  
  // 初始化Web管理界面（WiFi AP模式）
  webManager.begin("ESPMultiRF", "12345678");
  webManager.setCaptureModeCallback([](bool enabled) {
    if (enabled) {
      replayMode = true;
      currentLEDState = LED_BLINK;
      rf.enableCaptureMode();  // 确保库的捕获模式也被启用
      Serial.println("[WEB] 通过Web界面进入捕获模式");
      #if DEBUG_CAPTURE_MODE
      Serial.printf("[DEBUG] setCaptureModeCallback: replayMode=%d, rf.isCaptureMode()=%d\n",
                   replayMode, rf.isCaptureMode());
      Serial.printf("[DEBUG] setCaptureModeCallback: LED状态=%d (0=OFF,1=BLINK,2=ON)\n", currentLEDState);
      #endif
    } else {
      replayMode = false;
      rf.disableCaptureMode();
      #if DEBUG_CAPTURE_MODE
      Serial.printf("[DEBUG] setCaptureModeCallback: 退出捕获模式\n");
      #endif
    }
  });
  Serial.println("[WEB] Web管理界面已启动");
  Serial.printf("[WEB] 请连接WiFi: ESPMultiRF, 密码: 12345678\n");
  Serial.printf("[WEB] 然后访问: http://%s\n", webManager.getAPIP().c_str());
  
  Serial.println("ESPMultiRF库已初始化");
  Serial.printf("  协议: Protocol 1 (EV1527/PT2262)\n");
  Serial.printf("  脉冲长度: 320μs\n");
  Serial.printf("  重复次数: 5次\n");
  
  Serial.printf("\n发射引脚: GPIO%d\n", TX_PIN);
  Serial.printf("接收引脚: GPIO%d\n", RX_PIN);
  Serial.printf("复刻按钮: GPIO%d (短按发送复刻信号，长按2秒清空信号)\n", REPLAY_BUTTON_PIN);
  Serial.printf("LED指示灯: GPIO%d\n", LED_PIN);
  
  // 初始化复刻按钮GPIO（使用内部上拉电阻，按下时为LOW）
  pinMode(REPLAY_BUTTON_PIN, INPUT_PULLUP);
  
  // 初始化LED引脚（反向逻辑：HIGH熄灭，LOW常亮）
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);  // 启动时默认熄灭（反向：HIGH熄灭）
  currentLEDState = LED_OFF;
  
  // 从闪存加载信号
  Serial.println("\n[FLASH] 正在从闪存加载信号...");
  loadSignalFromFlash();
  
  // 如果没有复刻信号，自动进入复刻状态
  if (!signalCaptured) {
    replayMode = true;
    signalCaptured = false;
    capturedSignal = {"", ""};
    currentLEDState = LED_BLINK;  // 进入复刻模式，LED快闪
    Serial.println("\n[自动] 检测到没有复刻信号，自动进入复刻模式");
    Serial.println("[自动] LED指示灯快闪中，等待接收信号...");
  } else {
    Serial.println("[自动] 已从闪存恢复复刻信号，LED常亮");
  }
  
  // 硬件测试
  Serial.println("\n========================================");
  Serial.println("硬件测试:");
  pinMode(TX_PIN, OUTPUT);
  digitalWrite(TX_PIN, HIGH);
  delay(100);
  digitalWrite(TX_PIN, LOW);
  Serial.println("GPIO14输出测试: 完成");
  
  // 测试复刻按钮
  bool buttonState = digitalRead(REPLAY_BUTTON_PIN);
  Serial.printf("GPIO%d按钮状态: %s (当前: %s)\n", 
               REPLAY_BUTTON_PIN, 
               buttonState == HIGH ? "未按下(HIGH)" : "按下(LOW)",
               buttonState == HIGH ? "HIGH" : "LOW");
  Serial.printf("提示：按下boot按键（GPIO%d）可以发送复刻信号\n", REPLAY_BUTTON_PIN);
  
  Serial.printf("Serial1接收测试: 缓冲区字节数: %d\n", Serial1.available());
  delay(2000);
  if (Serial1.available() > 0) {
    Serial.println("接收模块检测到数据");
  } else {
    Serial.println("警告: 2秒内未检测到接收模块数据");
  }
  Serial.println("========================================");
  
  // 创建RTOS任务
  xTaskCreate(receiveTask, "ReceiveTask", 4096, NULL, 2, NULL);
  xTaskCreate(statusTask, "StatusTask", 2048, NULL, 1, NULL);
  xTaskCreate(buttonTask, "ButtonTask", 2048, NULL, 2, NULL);  // GPIO按钮检测任务
  xTaskCreate(ledTask, "LEDTask", 2048, NULL, 1, NULL);  // LED控制任务
  
  Serial.println("\nRTOS任务已启动，系统就绪");
  
  // 使用声明
  Serial.println("\n==================================================");
  Serial.println("⚠️  重要提示 - 请务必阅读");
  Serial.println("==================================================");
  Serial.println("本设备仅供学习、研究和个人合法使用");
  Serial.println("");
  Serial.println("✅ 允许：备份自己的遥控器、控制自己的设备");
  Serial.println("❌ 禁止：复制他人门禁、未授权访问、非法用途");
  Serial.println("");
  Serial.println("使用者需遵守当地法律法规和无线电管理规定");
  Serial.println("对使用本设备造成的后果自行承担全部法律责任");
  Serial.println("==================================================\n");
  
  Serial.println("复刻功能说明:");
  Serial.println("  - 系统启动时会自动从闪存加载保存的信号（关机不丢失）");
  Serial.printf("  - 短按boot按键（GPIO%d）发送绑定的信号\n", REPLAY_BUTTON_PIN);
  Serial.printf("  - 长按boot按键（GPIO%d）2秒可清空复刻信号\n", REPLAY_BUTTON_PIN);
  Serial.println("  - LED指示灯状态（反向逻辑：HIGH熄灭，LOW常亮）：");
  Serial.println("    * 熄灭（HIGH）：待机状态");
  Serial.println("    * 快闪：捕获模式，等待接收信号");
  Serial.println("    * 常亮（LOW）：已捕获信号");
  Serial.println("");
  Serial.println("📱 Web管理界面:");
  Serial.printf("  - WiFi SSID: %s\n", "ESPMultiRF");
  Serial.printf("  - WiFi密码: %s\n", "12345678");
  Serial.printf("  - 访问地址: http://%s\n", webManager.getAPIP().c_str());
  Serial.println("  - 功能: 捕获信号、发送信号、绑定Boot按钮、清空信号");
}

void loop() {
  // 处理Web请求
  webManager.handleClient();
  
  delay(100);
}
