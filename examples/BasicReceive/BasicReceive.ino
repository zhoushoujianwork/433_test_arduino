/*
 * ESP433RF - Basic Receive Example
 * 
 * This example demonstrates how to receive 433MHz signals
 * using various receiver modules (e.g., Ling-R1A).
 */

#include <ESP433RF.h>

// Create instance: TX pin, RX pin, baud rate
ESP433RF rf(14, 18, 9600);

// Receive callback function
void onReceive(RFSignal signal) {
  Serial.print("Received: ");
  Serial.print(signal.address);
  Serial.print(signal.key);
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("ESP433RF - Basic Receive Example");
  Serial.println("==================================");
  
  // Initialize library
  rf.begin(false);  // false = don't use RCSwitch
  
  // Set receive callback
  rf.setReceiveCallback(onReceive);
  
  Serial.println("Waiting for signals...");
  Serial.println();
}

void loop() {
  // Check for received signals
  if (rf.receiveAvailable()) {
    RFSignal signal;
    if (rf.receive(signal)) {
      Serial.print("Signal received: ");
      Serial.print(signal.address);
      Serial.print(signal.key);
      Serial.print(" (Send: ");
      Serial.print(rf.getSendCount());
      Serial.print(", Receive: ");
      Serial.print(rf.getReceiveCount());
      Serial.println(")");
    }
  }
  
  delay(10);
}

