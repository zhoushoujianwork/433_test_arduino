/*
 * ESP433RF - Basic Send Example
 * 
 * This example demonstrates how to send 433MHz signals
 * using GPIO mode or RCSwitch library.
 */

#include <ESP433RF.h>

// Create instance: TX pin, RX pin, baud rate
ESP433RF rf(14, 18, 9600);

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("ESP433RF - Basic Send Example");
  Serial.println("==================================");
  
  // Initialize library
  // Set to true to use RCSwitch library (requires RCSwitch library)
  // Set to false to use GPIO manual encoding
  rf.begin(false);  // false = GPIO mode
  
  // Configure encoding
  rf.setEncodingMode(ENC_NONE);
  rf.setInvertSignal(true);
  rf.setRepeatCount(5);
  rf.setTiming(380);  // 380 microseconds
  
  Serial.println("Sending test signal every 3 seconds...");
  Serial.println();
}

void loop() {
  // Send signal: address code (6 hex digits) + key (2 hex digits)
  RFSignal signal;
  signal.address = "62E7E8";
  signal.key = "31";
  
  Serial.print("Sending: ");
  Serial.print(signal.address);
  Serial.print(signal.key);
  Serial.print(" (Total sent: ");
  Serial.print(rf.getSendCount());
  Serial.println(")");
  
  rf.send(signal, 380);
  
  delay(3000);  // Wait 3 seconds
}

