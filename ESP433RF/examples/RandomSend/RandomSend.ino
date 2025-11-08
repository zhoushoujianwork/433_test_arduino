/*
 * ESP433RF - Random Send Example
 * 
 * This example demonstrates sending random signals
 * and verifying received signals.
 */

#include <ESP433RF.h>

// Create instance
ESP433RF rf(14, 18, 9600);

// Random signals array
RFSignal signals[] = {
  {"62E7E8", "31"},
  {"A3B4C5", "32"},
  {"D6E7F8", "33"},
  {"1A2B3C", "34"},
  {"4D5E6F", "35"},
  {"7A8B9C", "36"},
  {"AB12CD", "37"},
  {"EF34AB", "38"},
  {"5678EF", "39"},
  {"9ABC12", "3A"}
};

#define SIGNAL_COUNT (sizeof(signals) / sizeof(signals[0]))

RFSignal currentSent;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("ESP433RF - Random Send Example");
  Serial.println("==================================");
  
  // Initialize library with RCSwitch
  rf.begin(true);  // true = use RCSwitch library
  
  // Configure
  rf.setRepeatCount(5);
  rf.setTiming(320);
  
  // Initialize random seed
  randomSeed(analogRead(0));
  
  Serial.print("Signal count: ");
  Serial.println(SIGNAL_COUNT);
  Serial.println("Sending random signals every 5 seconds...");
  Serial.println();
}

void loop() {
  // Randomly select a signal
  int index = random(0, SIGNAL_COUNT);
  currentSent = signals[index];
  
  Serial.print("Sending [");
  Serial.print(index);
  Serial.print("]: ");
  Serial.print(currentSent.address);
  Serial.print(currentSent.key);
  Serial.println();
  
  rf.send(currentSent, 380);
  
  // Check for received signals
  delay(1000);  // Wait a bit for signal to be received
  
  if (rf.receiveAvailable()) {
    RFSignal received;
    if (rf.receive(received)) {
      Serial.print("Received: ");
      Serial.print(received.address);
      Serial.print(received.key);
      
      // Verify match
      if (received.address == currentSent.address && 
          received.key == currentSent.key) {
        Serial.println(" ✓ MATCH!");
      } else {
        Serial.println(" ✗ No match");
      }
    }
  }
  
  delay(4000);  // Total 5 seconds interval
}

