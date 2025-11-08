# ESP433RF

Universal ESP32 433MHz RF Transceiver Library for Arduino/PlatformIO

A comprehensive library for receiving and transmitting 433MHz signals using ESP32, with support for various receiver modules (Ling-R1A, etc.) and multiple transmitter modes (GPIO manual encoding or RCSwitch library).

## Features

- ✅ **Receive 433MHz signals** via Ling-R1A module (UART)
- ✅ **Send 433MHz signals** via GPIO (1527 encoding) or RCSwitch library
- ✅ **Multiple encoding modes** (none, nibble, byte, full 24-bit, LSB first)
- ✅ **Signal inversion** support
- ✅ **Callback support** for received signals
- ✅ **Statistics** (send/receive counters)
- ✅ **Configurable timing** and repeat count

## Hardware Requirements

- **ESP32** (ESP32, ESP32-S2, ESP32-S3, ESP32-C3)
- **433MHz receiver module** (e.g., Ling-R1A, etc.)
- **433MHz transmitter module** (for sending via GPIO)
- **Optional**: RCSwitch library for more reliable transmission

## Installation

### Arduino IDE

1. Download or clone this repository
2. Copy the `ESP433RF` folder to your Arduino `libraries` folder
3. Restart Arduino IDE

### PlatformIO

Add to your `platformio.ini`:

```ini
lib_deps = 
    https://github.com/zhoushoujianwork/ESP433RF.git
```

## Quick Start

### Basic Receive

```cpp
#include <ESP433RF.h>

ESP433RF rf(14, 18, 9600);  // TX pin, RX pin, baud rate

void setup() {
  Serial.begin(115200);
  rf.begin(false);  // false = GPIO mode
}

void loop() {
  if (rf.receiveAvailable()) {
    RFSignal signal;
    if (rf.receive(signal)) {
      Serial.print("Received: ");
      Serial.print(signal.address);
      Serial.println(signal.key);
    }
  }
}
```

### Basic Send (GPIO Mode)

```cpp
#include <ESP433RF.h>

ESP433RF rf(14, 18, 9600);

void setup() {
  Serial.begin(115200);
  rf.begin(false);  // GPIO mode
  rf.setTiming(380);    // 380 microseconds
  rf.setRepeatCount(5); // Repeat 5 times
}

void loop() {
  RFSignal signal;
  signal.address = "62E7E8";
  signal.key = "31";
  
  rf.send(signal, 380);
  delay(3000);
}
```

### Basic Send (RCSwitch Mode)

```cpp
#define USE_RCSWITCH 1  // Enable RCSwitch support
#include <ESP433RF.h>

ESP433RF rf(14, 18, 9600);

void setup() {
  Serial.begin(115200);
  rf.begin(true);  // true = RCSwitch mode
  rf.setTiming(320);
  rf.setRepeatCount(5);
}

void loop() {
  RFSignal signal;
  signal.address = "62E7E8";
  signal.key = "31";
  
  rf.send(signal);
  delay(3000);
}
```

## API Reference

### Constructor

```cpp
ESP433RF(uint8_t txPin = 14, uint8_t rxPin = 18, uint32_t baudRate = 9600)
```

### Methods

#### Initialization

- `void begin(bool useRCSwitch = false)` - Initialize the library
- `void end()` - Clean up resources

#### Receive

- `bool receiveAvailable()` - Check if data is available
- `bool receive(RFSignal &signal)` - Receive a signal
- `bool parseSignal(String data, RFSignal &signal)` - Parse signal from string

#### Send

- `void send(String address, String key, int timing = 380)` - Send signal
- `void send(RFSignal signal, int timing = 380)` - Send signal (struct)

#### Configuration

- `void setEncodingMode(EncodingMode mode)` - Set encoding mode
- `void setInvertSignal(bool invert)` - Set signal inversion
- `void setRepeatCount(uint8_t count)` - Set repeat count
- `void setTiming(int baseTiming)` - Set base timing (microseconds)
- `void setProtocol(uint8_t protocol)` - Set RCSwitch protocol
- `void setPulseLength(uint16_t pulseLength)` - Set pulse length

#### Status

- `uint32_t getSendCount()` - Get send counter
- `uint32_t getReceiveCount()` - Get receive counter
- `void resetCounters()` - Reset counters

#### Callback

- `void setReceiveCallback(ReceiveCallback callback)` - Set receive callback

## Encoding Modes

- `ENC_NONE` - No inversion (direct)
- `ENC_NIBBLE` - Nibble inversion
- `ENC_BYTE` - Byte inversion
- `ENC_FULL24` - Full 24-bit inversion
- `ENC_LSB_FIRST` - LSB first

## Signal Format

Signals are represented as:
- **Address**: 6-digit hexadecimal (e.g., "62E7E8")
- **Key**: 2-digit hexadecimal (e.g., "31")

Supported receive formats:
- `LC:XXXXXXYY` (Ling-R1A format)
- `RX:XXXXXXYY`
- Direct 8-digit hex: `XXXXXXYY`

## Examples

See the `examples` folder for:
- `BasicReceive` - Simple receive example
- `BasicSend` - Simple send example
- `RandomSend` - Random signal sending with verification

## Dependencies

- **Optional**: [RCSwitch](https://github.com/sui77/rc-switch) library for more reliable transmission

To use RCSwitch:
1. Install RCSwitch library
2. Define `USE_RCSWITCH` before including ESP433RF.h:
   ```cpp
   #define USE_RCSWITCH 1
   #include <ESP433RF.h>
   ```
3. Call `begin(true)` to enable RCSwitch mode

## License

MIT License - see LICENSE file for details

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## Author

Zhoushoujian

## Acknowledgments

- Supports various 433MHz receiver modules (Ling-R1A, etc.)
- Compatible with RCSwitch library for transmission
- Supports 1527 encoding protocol
