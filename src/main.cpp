#include <Arduino.h>
#include <HardwareSerial.h>
// 可选：使用RCSwitch库发送（更可靠的编码）
// 取消注释下面的行来启用RCSwitch库（需要先在platformio.ini中添加库依赖）
#define USE_RCSWITCH 1
#ifdef USE_RCSWITCH
#include <RCSwitch.h>
RCSwitch mySwitch = RCSwitch();
#endif

// 硬件引脚定义
#define TX_PIN 14       // 发射模块DATA引脚
#define RX_PIN 18       // 接收模块数据引脚

// 1527编码时序参数（微秒）
// 标准1527编码时序：基础脉宽约320-380μs
#define T_BASE 320      // 基础时间单位（标准值）
#define T0_HIGH T_BASE        // 逻辑0：短高+长低
#define T0_LOW (T_BASE * 3)   
#define T1_HIGH (T_BASE * 3)  // 逻辑1：长高+短低
#define T1_LOW T_BASE         
#define SYNC_HIGH (T_BASE * 31) // 同步码：很长高+短低
#define SYNC_LOW T_BASE

// 测试配置
#define TEST_ADDRESS "62E7E8"
#define TEST_KEY "31"
#define SEND_INTERVAL 5000  // 发送间隔5秒

// 随机地址码数组（10个）
#define RANDOM_SIGNAL_COUNT 10
struct RandomSignal {
  String address;
  String key;
  String name;
};

RandomSignal randomSignals[RANDOM_SIGNAL_COUNT] = {
  {"62E7E8", "31", "信号1"},
  {"A3B4C5", "32", "信号2"},
  {"D6E7F8", "33", "信号3"},
  {"1A2B3C", "34", "信号4"},
  {"4D5E6F", "35", "信号5"},
  {"7A8B9C", "36", "信号6"},
  {"AB12CD", "37", "信号7"},
  {"EF34AB", "38", "信号8"},
  {"5678EF", "39", "信号9"},
  {"9ABC12", "3A", "信号10"}
};

// 当前发送的信号（用于验证）
String currentSentAddress = "";
String currentSentKey = "";

// 发送模式：0=GPIO手动编码, 1=RCSwitch库
#ifndef USE_RCSWITCH
#define SEND_MODE 0  // 默认使用GPIO方式
#else
#define SEND_MODE 1  // 使用RCSwitch库
#endif

// 时序测试数组 - 重点测试有效的380μs
int timingTests[] = {380, 380, 380, 380, 380};  // 固定使用380μs
int currentTimingIndex = 0;
bool invertSignal = true;  // 固定使用信号反转

// 全局变量
static int sendCount = 0;
static int receiveCount = 0;
static bool testPassed = false;

// 十六进制字符转数值
uint8_t hexToNum(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return 0;
}

// 编码模式：0=无反转, 1=半字节反转, 2=字节反转, 3=全24位反转, 4=LSB优先
static int encodeMode = 0;

// 反转4位半字节的位序
uint8_t reverseBits4(uint8_t val) {
  return ((val & 0x1) << 3) | ((val & 0x2) << 1) | ((val & 0x4) >> 1) | ((val & 0x8) >> 3);
}

// 反转整个24位数据的位序
uint32_t reverseBits24(uint32_t data) {
  uint32_t result = 0;
  for (int i = 0; i < 24; i++) {
    if (data & (1 << i)) {
      result |= (1 << (23 - i));
    }
  }
  return result;
}

// 转换为24位数据 - 支持多种编码模式
uint32_t hexTo24bit(String address, String key) {
  uint32_t data = 0;
  String fullHex = address + key;  // 62E7E831
  
  // 模式0: 直接组合，不反转
  if (encodeMode == 0) {
    for (int i = 0; i < 8; i++) {
      char c = fullHex.charAt(i);
      uint8_t val = hexToNum(c);
      data = (data << 4) | val;
    }
  }
  // 模式1: 半字节反转
  else if (encodeMode == 1) {
    for (int i = 0; i < 8; i++) {
      char c = fullHex.charAt(i);
      uint8_t val = hexToNum(c);
      val = reverseBits4(val);
      data = (data << 4) | val;
    }
  }
  // 模式2: 字节反转（交换字节顺序）
  else if (encodeMode == 2) {
    uint8_t bytes[3];
    for (int i = 0; i < 3; i++) {
      bytes[i] = (hexToNum(fullHex.charAt(i*2)) << 4) | hexToNum(fullHex.charAt(i*2+1));
    }
    // 反转字节顺序
    data = ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[1] << 8) | bytes[0];
  }
  // 模式3: 全24位反转
  else if (encodeMode == 3) {
    for (int i = 0; i < 8; i++) {
      char c = fullHex.charAt(i);
      uint8_t val = hexToNum(c);
      data = (data << 4) | val;
    }
    data = reverseBits24(data);
  }
  // 模式4: LSB优先（从低位到高位）
  else if (encodeMode == 4) {
    for (int i = 7; i >= 0; i--) {
      char c = fullHex.charAt(i);
      uint8_t val = hexToNum(c);
      data = (data << 4) | val;
    }
  }
  
  return data & 0xFFFFFF;  // 只保留24位
}

// 发送单个位（支持信号反转）
void sendBit(bool bit, int timing) {
  bool high = invertSignal ? LOW : HIGH;
  bool low = invertSignal ? HIGH : LOW;
  
  if (bit) {
    digitalWrite(TX_PIN, high);
    delayMicroseconds(timing * 3);
    digitalWrite(TX_PIN, low);
    delayMicroseconds(timing);
  } else {
    digitalWrite(TX_PIN, high);
    delayMicroseconds(timing);
    digitalWrite(TX_PIN, low);
    delayMicroseconds(timing * 3);
  }
}

// 发送完整信号（支持可变时序和RCSwitch库）
void sendSignal(String address, String key, int timing) {
#ifdef USE_RCSWITCH
  // 使用RCSwitch库发送（更可靠）
  if (SEND_MODE == 1) {
    // RCSwitch方式：将地址码和按键值转换为数据
    // 注意：62E7E831是32位（4字节），但1527编码是24位
    // 需要按照1527编码格式：19位地址码 + 5位按键值 = 24位
    String fullHex = address + key;
    
    // 解析为32位数据
    uint32_t fullData = 0;
    for (int i = 0; i < 8; i++) {
      uint8_t val = hexToNum(fullHex.charAt(i));
      fullData = (fullData << 4) | val;
    }
    
    // 1527编码：提取24位数据（高19位地址码 + 低5位按键值）
    // 方法1：直接使用后24位（如果接收模块期望这种格式）
    uint32_t code24bit = fullData & 0xFFFFFF;  // 后24位：E7E831
    
    // 方法2：按照1527编码格式（19位地址 + 5位按键）
    // 从fullData提取：高19位作为地址码，按键值的低5位
    uint32_t address19bit = (fullData >> 5) & 0x7FFFF;  // 高19位地址码
    uint8_t key5bit = fullData & 0x1F;  // 低5位按键值
    uint32_t code1527 = (address19bit << 5) | key5bit;  // 1527格式：24位
    
    Serial.printf("[SEND] RCSwitch发送 - 原始:0x%08lX, 24位:0x%06lX, 1527格式:0x%06lX\n", 
                  fullData, code24bit, code1527);
    Serial.printf("[SEND] 地址:%s, 按键:%s (19位地址:0x%05lX, 5位按键:0x%02X)\n", 
                  address.c_str(), key.c_str(), address19bit, key5bit);
    
    // 尝试两种方式：先使用后24位方式（与接收到的E7E83100前6位匹配）
    // 如果不行，可以尝试code1527
    mySwitch.send(code24bit, 24);
    Serial.printf("[SEND] 已发送24位数据: 0x%06lX (重复发送5次)\n", code24bit);
    return;
  }
#endif
  
  // GPIO方式：手动编码发送
  uint32_t data = hexTo24bit(address, key);
  bool high = invertSignal ? LOW : HIGH;
  bool low = invertSignal ? HIGH : LOW;
  
  Serial.printf("[SEND] GPIO发送 - 编码模式:%d, 数据:0x%06lX\n", encodeMode, data & 0xFFFFFF);
  
  // 发送同步码
  digitalWrite(TX_PIN, high);
  delayMicroseconds(timing * 31);
  digitalWrite(TX_PIN, low);
  delayMicroseconds(timing);
  
  // 发送24位数据 - 根据编码模式选择发送顺序
  if (encodeMode == 4) {
    // LSB优先：从低位到高位
    for (int i = 0; i < 24; i++) {
      bool bit = (data >> i) & 0x01;
      sendBit(bit, timing);
    }
  } else {
    // MSB优先：从高位到低位（默认）
    for (int i = 23; i >= 0; i--) {
      bool bit = (data >> i) & 0x01;
      sendBit(bit, timing);
    }
  }
  
  // 结束
  digitalWrite(TX_PIN, low);
  delayMicroseconds(10000);
}

// 解析接收信号
bool parseSignal(String data, String &address, String &key) {
  data.trim();
  Serial.printf("[PARSE] 尝试解析: '%s' (长度:%d)\n", data.c_str(), data.length());
  
  // 格式1: LC:XXXXXXYY
  if (data.startsWith("LC:") && data.length() >= 11) {
    String signal = data.substring(3, 11);
    address = signal.substring(0, 6);
    key = signal.substring(6, 8);
    address.toUpperCase();
    key.toUpperCase();
    Serial.printf("[PARSE] LC格式解析成功: %s + %s\n", address.c_str(), key.c_str());
    return true;
  }
  
  // 格式2: RX:XXXXXXYY
  if (data.startsWith("RX:") && data.length() >= 11) {
    String signal = data.substring(3, 11);
    address = signal.substring(0, 6);
    key = signal.substring(6, 8);
    address.toUpperCase();
    key.toUpperCase();
    Serial.printf("[PARSE] RX格式解析成功: %s + %s\n", address.c_str(), key.c_str());
    return true;
  }
  
  // 格式3: 直接8位十六进制
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
      address = data.substring(0, 6);
      key = data.substring(6, 8);
      address.toUpperCase();
      key.toUpperCase();
      Serial.printf("[PARSE] 直接十六进制解析成功: %s + %s\n", address.c_str(), key.c_str());
      return true;
    }
  }
  
  Serial.printf("[PARSE] 所有格式解析失败\n");
  return false;
}

// 发送任务 - 随机发送10个地址码
void sendTask(void *parameter) {
  while (true) {
    sendCount++;
    
    // 随机选择一个信号
    int randomIndex = random(0, RANDOM_SIGNAL_COUNT);
    RandomSignal selected = randomSignals[randomIndex];
    
    // 保存当前发送的信号，用于验证
    currentSentAddress = selected.address;
    currentSentKey = selected.key;
    
    Serial.printf("\n[SEND] 第%d次发送 [%s]: %s%s\n", 
                  sendCount, selected.name.c_str(), 
                  selected.address.c_str(), selected.key.c_str());
    
#ifdef USE_RCSWITCH
    if (SEND_MODE == 1) {
      Serial.printf("[SEND] 使用RCSwitch库发送（自动重复5次）\n");
      sendSignal(selected.address, selected.key, 380);
    } else {
      Serial.printf("[SEND] 使用GPIO方式发送（固定参数: 380μs + 信号反转）\n");
      uint32_t testData = hexTo24bit(selected.address, selected.key);
      Serial.printf("[SEND] 编码数据: 0x%08lX (24位: 0x%06lX)\n", testData, testData & 0xFFFFFF);
      
      // GPIO方式：重复发送3次
      for (int i = 0; i < 3; i++) {
        Serial.printf("[SEND] 重复 %d/3\n", i+1);
        sendSignal(selected.address, selected.key, 380);
        delay(100);
      }
    }
#else
    Serial.printf("[SEND] 使用GPIO方式发送（固定参数: 380μs + 信号反转）\n");
    uint32_t testData = hexTo24bit(selected.address, selected.key);
    Serial.printf("[SEND] 编码数据: 0x%08lX (24位: 0x%06lX)\n", testData, testData & 0xFFFFFF);
    
    // GPIO方式：重复发送3次
    for (int i = 0; i < 3; i++) {
      Serial.printf("[SEND] 重复 %d/3\n", i+1);
      sendSignal(selected.address, selected.key, 380);
      delay(100);
    }
#endif
    
    vTaskDelay(pdMS_TO_TICKS(SEND_INTERVAL));
  }
}

// 接收任务
void receiveTask(void *parameter) {
  String buffer = "";
  unsigned long lastDebugTime = 0;
  int totalBytes = 0;
  
  while (true) {
    // 每5秒输出调试信息
    if (millis() - lastDebugTime > 5000) {
      Serial.printf("[DEBUG] 串口状态 - 总接收字节:%d, 缓冲区长度:%d\n", totalBytes, buffer.length());
      Serial.printf("[DEBUG] Serial1可用字节: %d\n", Serial1.available());
      lastDebugTime = millis();
    }
    
    if (Serial1.available()) {
      char c = Serial1.read();
      totalBytes++;
      
      // 输出原始字节（十六进制和字符）
      Serial.printf("[RAW] 0x%02X ('%c')\n", (uint8_t)c, (c >= 32 && c <= 126) ? c : '.');
      
      if (c == '\n' || c == '\r') {
        if (buffer.length() > 0) {
          Serial.printf("[RECV] 完整数据: '%s' (长度:%d)\n", buffer.c_str(), buffer.length());
          
          String address, key;
          if (parseSignal(buffer, address, key)) {
            receiveCount++;
            Serial.printf("[RECV] 第%d次接收: %s%s\n", receiveCount, address.c_str(), key.c_str());
            
            // 验证是否匹配（与当前发送的随机信号比较）
            if (currentSentAddress.length() == 0) {
              Serial.println("[TEST] 警告：当前没有发送信号记录，无法验证");
            } else {
              // 计算发送的实际24位数据（RCSwitch发送的是后24位）
              String fullHex = currentSentAddress + currentSentKey;
              uint32_t sentFullData = 0;
              for (int i = 0; i < 8 && i < fullHex.length(); i++) {
                uint8_t val = hexToNum(fullHex.charAt(i));
                sentFullData = (sentFullData << 4) | val;
              }
              uint32_t sent24bit = sentFullData & 0xFFFFFF;  // 后24位
              
              // 将接收到的地址码转换为数值
              uint32_t recvAddress = 0;
              for (int i = 0; i < address.length() && i < 6; i++) {
                uint8_t val = hexToNum(address.charAt(i));
                recvAddress = (recvAddress << 4) | val;
              }
              
              // 验证方式1：完全匹配（地址码+按键值）
              bool matchFull = (address == currentSentAddress && key == currentSentKey);
              
              // 验证方式2：24位数据匹配（接收到的地址码匹配发送的24位数据）
              bool match24bit = (recvAddress == sent24bit);
              
              // 验证方式3：接收到的地址码是发送24位数据的后6位十六进制
              char sentHex[7];
              sprintf(sentHex, "%06lX", sent24bit);
              bool matchHex = (address == String(sentHex));
              
              if (matchFull) {
                testPassed = true;
                Serial.printf("[TEST] ✓ 验证通过！完全匹配（期望:%s%s, 接收:%s%s）\n", 
                            currentSentAddress.c_str(), currentSentKey.c_str(),
                            address.c_str(), key.c_str());
              } else if (match24bit || matchHex) {
                testPassed = true;
                Serial.printf("[TEST] ✓ 验证通过！24位数据匹配\n");
                Serial.printf("[TEST]   期望:%s%s (24位:0x%06lX), 接收:%s%s\n", 
                            currentSentAddress.c_str(), currentSentKey.c_str(),
                            sent24bit, address.c_str(), key.c_str());
              } else {
                Serial.printf("[TEST] ✗ 验证失败！\n");
                Serial.printf("[TEST]   期望:%s%s (24位:0x%06lX %s)\n", 
                            currentSentAddress.c_str(), currentSentKey.c_str(),
                            sent24bit, sentHex);
                Serial.printf("[TEST]   接收:%s%s (地址码:0x%06lX)\n", 
                            address.c_str(), key.c_str(), recvAddress);
              }
            }
          } else {
            Serial.printf("[PARSE] 解析失败: '%s'\n", buffer.c_str());
          }
          
          buffer = "";
        }
      } else {
        buffer += c;
        if (buffer.length() > 64) {
          Serial.printf("[WARN] 缓冲区溢出，清空: '%s'\n", buffer.c_str());
          buffer = "";
        }
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// 状态监控任务
void statusTask(void *parameter) {
  while (true) {
    Serial.printf("[STATUS] 发送:%d次, 接收:%d次, 测试:%s\n", 
                  sendCount, receiveCount, testPassed ? "通过" : "进行中");
    
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("========================================");
  Serial.println("ESP32 433MHz 收发测试 (RTOS版本)");
  Serial.println("========================================");
  
  // 初始化硬件
  pinMode(TX_PIN, OUTPUT);
  digitalWrite(TX_PIN, LOW);
  Serial1.begin(9600, SERIAL_8N1, RX_PIN, -1);
  
#ifdef USE_RCSWITCH
  // 初始化RCSwitch库
  if (SEND_MODE == 1) {
    mySwitch.enableTransmit(TX_PIN);
    mySwitch.setProtocol(1);  // Protocol 1 = EV1527/PT2262兼容
    mySwitch.setPulseLength(320);  // 脉冲长度320μs（与当前时序匹配）
    mySwitch.setRepeatTransmit(5);  // 重复发送5次（提高接收成功率）
    Serial.println("RCSwitch库已初始化");
    Serial.printf("  协议: Protocol 1 (EV1527/PT2262)\n");
    Serial.printf("  脉冲长度: 320μs\n");
    Serial.printf("  重复次数: 5次\n");
  }
#endif
  
  Serial.printf("发射引脚: GPIO%d\n", TX_PIN);
  Serial.printf("接收引脚: GPIO%d\n", RX_PIN);
  Serial.printf("测试模式: 随机发送10个地址码\n");
  Serial.printf("发送间隔: %d秒\n", SEND_INTERVAL/1000);
  Serial.println("随机地址码列表:");
  for (int i = 0; i < RANDOM_SIGNAL_COUNT; i++) {
    Serial.printf("  [%s] %s%s\n", randomSignals[i].name.c_str(), 
                  randomSignals[i].address.c_str(), randomSignals[i].key.c_str());
  }
  
  // 初始化随机种子
  randomSeed(analogRead(0));
  
  // 硬件测试
  Serial.println("========================================");
  Serial.println("硬件测试:");
  Serial.printf("GPIO%d输出测试: ", TX_PIN);
  digitalWrite(TX_PIN, HIGH);
  delay(100);
  digitalWrite(TX_PIN, LOW);
  Serial.println("完成");
  
  Serial.printf("Serial1接收测试: ");
  Serial1.flush();
  delay(100);
  Serial.printf("缓冲区字节数: %d\n", Serial1.available());
  
  // 串口回环测试（如果有连接）
  Serial.println("等待接收模块数据...");
  unsigned long testStart = millis();
  bool dataReceived = false;
  while (millis() - testStart < 2000) {
    if (Serial1.available()) {
      Serial.printf("检测到数据: 0x%02X\n", Serial1.read());
      dataReceived = true;
      break;
    }
    delay(10);
  }
  if (!dataReceived) {
    Serial.println("警告: 2秒内未检测到接收模块数据");
  }
  
  Serial.println("========================================");
  
  // 创建RTOS任务
  xTaskCreate(sendTask, "SendTask", 4096, NULL, 2, NULL);
  xTaskCreate(receiveTask, "ReceiveTask", 4096, NULL, 2, NULL);
  xTaskCreate(statusTask, "StatusTask", 2048, NULL, 1, NULL);
  
  Serial.println("RTOS任务已启动，开始收发测试...");
}

void loop() {
  // 处理串口命令
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    
    if (cmd == "test") {
      Serial.println("手动发送测试信号...");
      for (int i = 0; i < 3; i++) {
        Serial.printf("发送 %d/3\n", i+1);
        sendSignal(TEST_ADDRESS, TEST_KEY, timingTests[currentTimingIndex]);
        delay(100);
      }
    } else if (cmd == "flip") {
      invertSignal = !invertSignal;
      Serial.printf("信号反转: %s\n", invertSignal ? "是" : "否");
    } else if (cmd.startsWith("timing:")) {
      int newTiming = cmd.substring(7).toInt();
      if (newTiming > 0) {
        Serial.printf("测试时序 %dμs...\n", newTiming);
        for (int i = 0; i < 3; i++) {
          sendSignal(TEST_ADDRESS, TEST_KEY, newTiming);
          delay(100);
        }
      }
     } else if (cmd == "status") {
       Serial.printf("发送:%d次, 接收:%d次, 测试:%s\n", 
                     sendCount, receiveCount, testPassed ? "通过" : "进行中");
       Serial.printf("当前时序:%dμs, 信号反转:%s, 编码模式:%d\n", 
                     timingTests[currentTimingIndex], invertSignal ? "是" : "否", encodeMode);
     } else if (cmd.startsWith("mode:")) {
       int newMode = cmd.substring(5).toInt();
       if (newMode >= 0 && newMode <= 4) {
         encodeMode = newMode;
         Serial.printf("编码模式设置为: %d\n", encodeMode);
         Serial.println("模式说明:");
         Serial.println("  0=无反转（直接）");
         Serial.println("  1=半字节反转");
         Serial.println("  2=字节反转");
         Serial.println("  3=全24位反转");
         Serial.println("  4=LSB优先");
       }
     }
  }
  
  delay(100);
}
