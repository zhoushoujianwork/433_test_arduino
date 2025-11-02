#include <Arduino.h>
#include <HardwareSerial.h>
#include "esp_system.h"

// 硬件引脚定义
#define UART2_RX_PIN 16  // 灵-R1 串口输出（接收）
#define UART2_TX_PIN 17  // 灵-T3MAX 串口输入（发送）
#define BUTTON_A_PIN 22  // 触发按键A（大门A）
#define BUTTON_B_PIN 23  // 触发按键B（大门B）

// UART2 配置（灵-R1和灵-T3MAX均使用9600bps）
#define UART2_BAUD 9600

// 遥控信号存储结构体
typedef struct {
  String address;  // 地址码（6位十六进制）
  String key;      // 按键值（2位十六进制）
  bool valid;      // 是否有效
} RemoteSignal;

// 存储多个遥控信号（最多支持10个）
#define MAX_SIGNALS 10
RemoteSignal signals[MAX_SIGNALS];
int signalCount = 0;

// 当前选中的信号索引
int currentSignalIndex = 0;

// 串口接收缓冲区
String uart2Buffer = "";

// 打印重启原因
void printResetReason() {
  esp_reset_reason_t reason = esp_reset_reason();
  const char* reason_str;
  
  switch(reason) {
    case ESP_RST_UNKNOWN:    reason_str = "未知原因"; break;
    case ESP_RST_POWERON:    reason_str = "上电复位"; break;
    case ESP_RST_EXT:        reason_str = "外部复位"; break;
    case ESP_RST_SW:         reason_str = "软件复位"; break;
    case ESP_RST_PANIC:      reason_str = "异常/断言复位"; break;
    case ESP_RST_INT_WDT:    reason_str = "中断看门狗复位"; break;
    case ESP_RST_TASK_WDT:   reason_str = "任务看门狗复位"; break;
    case ESP_RST_WDT:        reason_str = "其他看门狗复位"; break;
    case ESP_RST_DEEPSLEEP:  reason_str = "深度睡眠唤醒"; break;
    case ESP_RST_BROWNOUT:   reason_str = "欠压复位"; break;
    case ESP_RST_SDIO:       reason_str = "SDIO复位"; break;
    default:                 reason_str = "未定义"; break;
  }
  
  Serial.printf("[RESET] 重启原因: %d (%s)\n", reason, reason_str);
  
  if (reason == ESP_RST_TASK_WDT || reason == ESP_RST_INT_WDT || reason == ESP_RST_WDT) {
    Serial.println("[RESET] 检测到看门狗复位！");
  }
}

// 初始化信号存储数组
void initSignals() {
  for (int i = 0; i < MAX_SIGNALS; i++) {
    signals[i].address = "";
    signals[i].key = "";
    signals[i].valid = false;
  }
  signalCount = 0;
  currentSignalIndex = 0;
}

// 解析灵-R1输出的串口数据（格式：LC:XXXXXXYY\r\n）
// XXXXXX为地址码（6位十六进制），YY为按键值（2位十六进制，含校验）
void parseRemoteCode(String data) {
  // 检查数据格式：必须以LC:开头
  if (!data.startsWith("LC:")) {
    Serial.printf("[PARSE] 无效格式，忽略: %s\n", data.c_str());
    return;
  }
  
  // 提取地址码（LC:后6位）
  if (data.length() < 9) {
    Serial.printf("[PARSE] 数据长度不足，忽略: %s\n", data.c_str());
    return;
  }
  
  String addr = data.substring(3, 9);  // 提取地址码（6位）
  String key = data.substring(9, 11);  // 提取按键值（2位）
  
  Serial.printf("[PARSE] 解析到信号 - 地址码: %s, 按键值: %s\n", addr.c_str(), key.c_str());
  
  // 检查是否已存在相同的信号
  bool found = false;
  for (int i = 0; i < signalCount; i++) {
    if (signals[i].address == addr && signals[i].key == key) {
      Serial.printf("[PARSE] 信号已存在（索引 %d），跳过存储\n", i);
      found = true;
      break;
    }
  }
  
  // 如果不存在且未满，则存储新信号
  if (!found && signalCount < MAX_SIGNALS) {
    signals[signalCount].address = addr;
    signals[signalCount].key = key;
    signals[signalCount].valid = true;
    signalCount++;
    Serial.printf("[PARSE] 信号已存储（索引 %d），当前共存储 %d 个信号\n", signalCount - 1, signalCount);
  } else if (signalCount >= MAX_SIGNALS) {
    Serial.println("[PARSE] 存储已满，无法存储更多信号！");
  }
}

// 通过灵-T3MAX发送遥控信号
// 发送格式：TX:XXXXXXYY（地址码6位 + 按键值2位）
void sendRemoteCode(String address, String key) {
  if (address.length() != 6 || key.length() != 2) {
    Serial.printf("[SEND] 无效信号格式，地址码:%s, 按键值:%s\n", address.c_str(), key.c_str());
    return;
  }
  
  String command = "TX:" + address + key;
  Serial2.println(command);  // 发送至灵-T3MAX
  Serial.printf("[SEND] 已发送遥控信号: %s\n", command.c_str());
  Serial.flush();
}

// 发送当前选中的信号
void sendCurrentSignal() {
  if (signalCount == 0) {
    Serial.println("[SEND] 未存储任何信号，无法发送！");
    return;
  }
  
  if (currentSignalIndex >= signalCount) {
    currentSignalIndex = 0;
  }
  
  if (signals[currentSignalIndex].valid) {
    Serial.printf("[SEND] 发送信号索引 %d: 地址码=%s, 按键值=%s\n", 
                  currentSignalIndex, 
                  signals[currentSignalIndex].address.c_str(),
                  signals[currentSignalIndex].key.c_str());
    sendRemoteCode(signals[currentSignalIndex].address, signals[currentSignalIndex].key);
  } else {
    Serial.println("[SEND] 当前信号无效！");
  }
}

// 显示所有存储的信号
void printStoredSignals() {
  Serial.println("\n=== 已存储的遥控信号 ===");
  if (signalCount == 0) {
    Serial.println("（暂无信号）");
  } else {
    for (int i = 0; i < signalCount; i++) {
      Serial.printf("[%d] 地址码: %s, 按键值: %s", 
                    i, 
                    signals[i].address.c_str(),
                    signals[i].key.c_str());
      if (i == currentSignalIndex) {
        Serial.print(" ← 当前选中");
      }
      Serial.println();
    }
  }
  Serial.println("======================\n");
}

void setup() {
  // ESP32-S3 USB CDC 串口初始化
  Serial.begin(115200);
  
  // ESP32-S3 需要足够的初始化延迟时间（至少2-3秒）
  // USB CDC 串口需要时间初始化，在初始化完成前发送的数据会被丢弃
  delay(3000);

  Serial.println("\n\n========================================");
  Serial.println("ESP32-S3 遥控信号采集与发射系统");
  Serial.println("灵-R1 + 灵-T3MAX 方案");
  Serial.println("========================================");
  Serial.flush();
  
  // 打印重启原因（诊断用）
  printResetReason();
  Serial.flush();
  
  Serial.printf("[BOOT] Setup 开始执行\n");
  Serial.printf("[BOOT] 可用堆内存: %lu 字节\n", ESP.getFreeHeap());
  Serial.printf("[BOOT] CPU 频率: %d MHz\n", ESP.getCpuFreqMHz());
  Serial.flush();

  // 初始化信号存储
  initSignals();
  Serial.println("[BOOT] 信号存储数组已初始化");

  // 初始化 UART2（用于与灵-R1和灵-T3MAX通信）
  // RX=GPIO16（接收灵-R1数据），TX=GPIO17（发送至灵-T3MAX）
  Serial2.begin(UART2_BAUD, SERIAL_8N1, UART2_RX_PIN, UART2_TX_PIN);
  Serial.printf("[BOOT] UART2 已初始化（%d bps, RX=GPIO%d, TX=GPIO%d）\n", 
                UART2_BAUD, UART2_RX_PIN, UART2_TX_PIN);
  Serial.flush();

  // 初始化按键引脚（可选，用于触发发送）
  pinMode(BUTTON_A_PIN, INPUT_PULLUP);
  pinMode(BUTTON_B_PIN, INPUT_PULLUP);
  Serial.printf("[BOOT] 按键引脚已初始化（A=GPIO%d, B=GPIO%d）\n", BUTTON_A_PIN, BUTTON_B_PIN);
  Serial.flush();

  // 输出配置信息
  Serial.println("\n=== 系统配置 ===");
  Serial.printf("UART2 RX (灵-R1): GPIO%d\n", UART2_RX_PIN);
  Serial.printf("UART2 TX (灵-T3MAX): GPIO%d\n", UART2_TX_PIN);
  Serial.printf("按键A (大门A): GPIO%d\n", BUTTON_A_PIN);
  Serial.printf("按键B (大门B): GPIO%d\n", BUTTON_B_PIN);
  Serial.printf("最大信号存储数: %d\n", MAX_SIGNALS);
  Serial.println("\n[BOOT] 系统初始化完成！");
  Serial.println("\n使用说明：");
  Serial.println("1. 使用灵-R1接收遥控信号，系统自动解析并存储");
  Serial.println("2. 按下按键A或按键B发送存储的信号");
  Serial.println("3. 通过串口监控查看采集的信号");
  Serial.println("\n");
  Serial.flush();
  
  delay(200);
}

void loop() {
  static unsigned long lastHeartbeat = 0;
  static bool lastButtonA = HIGH;
  static bool lastButtonB = HIGH;
  unsigned long currentTime = millis();

  // 接收灵-R1的串口数据
  while (Serial2.available() > 0) {
    char c = Serial2.read();
    if (c == '\n' || c == '\r') {
      if (uart2Buffer.length() > 0) {
        // 解析接收到的数据
        parseRemoteCode(uart2Buffer);
        // 显示当前所有存储的信号
        printStoredSignals();
        uart2Buffer = "";
      }
    } else {
      uart2Buffer += c;
      // 防止缓冲区溢出
      if (uart2Buffer.length() > 50) {
        Serial.println("[UART2] 缓冲区溢出，清空");
        uart2Buffer = "";
      }
    }
  }

  // 检测按键A（大门A）
  bool buttonA = digitalRead(BUTTON_A_PIN);
  if (buttonA == LOW && lastButtonA == HIGH) {
    // 按键按下（下降沿）
    delay(50);  // 消抖
    if (digitalRead(BUTTON_A_PIN) == LOW) {
      Serial.println("[BUTTON] 按键A按下（大门A）");
      if (signalCount > 0) {
        currentSignalIndex = 0;  // 使用第一个信号
        sendCurrentSignal();
      } else {
        Serial.println("[BUTTON] 未存储信号，无法发送！");
      }
    }
  }
  lastButtonA = buttonA;

  // 检测按键B（大门B）
  bool buttonB = digitalRead(BUTTON_B_PIN);
  if (buttonB == LOW && lastButtonB == HIGH) {
    // 按键按下（下降沿）
    delay(50);  // 消抖
    if (digitalRead(BUTTON_B_PIN) == LOW) {
      Serial.println("[BUTTON] 按键B按下（大门B）");
      if (signalCount > 1) {
        currentSignalIndex = 1;  // 使用第二个信号
        sendCurrentSignal();
      } else if (signalCount == 1) {
        currentSignalIndex = 0;  // 只有一个信号时也发送
        sendCurrentSignal();
      } else {
        Serial.println("[BUTTON] 未存储信号，无法发送！");
      }
    }
  }
  lastButtonB = buttonB;

  // 定期输出心跳，确认系统正常运行
  if (currentTime - lastHeartbeat >= 10000) {  // 每10秒输出一次心跳
    Serial.printf("[HEARTBEAT] 系统运行正常，运行时间: %lu ms, 已存储信号数: %d\n", 
                  currentTime, signalCount);
    Serial.flush();
    lastHeartbeat = currentTime;
  }

  delay(10);  // 短暂延时，避免CPU占用过高
}
