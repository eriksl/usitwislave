
# usitwislave

A simple i2c/twi slave implementation using the USI module found on several attiny models.
  
It is loosely based on the work by Atmel ([Application Note 312](http://ww1.microchip.com/downloads/en/AppNotes/Atmel-2560-Using-the-USI-Module-as-a-I2C-Slave_ApplicationNote_AVR312.pdf)) and [work by Donald Blake](https://repo.fo.am/dave/weavingcodes/-/tree/231603ef72ab30d45e0145ec4ed28085a29e2e5b/avr/attiny85/tinywires) (several fixes to the Atmel code, these are included in this code).

## Usage
Usage is quite simple: in your main loop, after all initialisations are done, call `usi_twi_slave(slave_address, use_sleep, data_callback, idle_callback)` where:
* `slave_address` is this slave device's address
* `use_sleep` allows usitwislave to sleep when it's not doing anything. Setting this to zero keeps the device awake, setting this to >0 allows sleeping. When `use_sleep` is != 0, the `idle_callback` is called less frequently, so if you're relying on `idle_callback` leave this at zero. This setting can reduce power draw.
* `data_callback` is a pointer to a function that is called when a stop condition occurs after a valid transaction has been completed. When a request is made from the master, this function fires. See below for details.
* `idle_callback` is an _optional_ callback that is be called when there is nothing to do (assuming `use_sleep` is set to 0). It's defined as `void idle_callback(void)`. If you're not using it, specify NULL.

### data_callbackdata
The data_callback is a function with the following definition:
```
static void twi_callback(volatile uint8_t input_buffer_length, const uint8_t *input_buffer, uint8_t  *output_buffer_length, uint8_t *output_buffer)
```
| Parameter | Purpose |
| ---: | :--- |
| input_buffer_length | the amount of bytes received from the master |
| input_buffer | the bytes received from the master |
| output_buffer_length | the amount of bytes you want to send back to the master |
| output_buffer | an array of bytes you want to send back to the master |

The input buffer is cleared after every valid transaction so you'll never see the same bytes from the master twice. The output buffer is cleared after all bytes are sent as well.  

## Example use
```c
#include <stdint.h>
#include "usitwislave.h"

#define DEVICE_ADDRESS	0x10

uint8_t regs[4] = { 4, 8, 16, 32 };
uint8_t currentReg = 0;

static void request(volatile uint8_t input_buffer_length, const uint8_t *input_buffer,
        uint8_t *output_buffer_length, uint8_t *output_buffer) {
  // the number of bytes to be returned to the master
  *output_buffer_length = 1;
  
  // the data to be returned
  output_buffer[0] = regs[currentReg];
  
  // send the next reg next time
  currentReg++;
  if (currentReg > sizeof(regs)) {
    currentReg = 0;
  }
  
  // If you have multiple bytes, return each one:
  output_buffer[0] = regs[currentReg];
  output_buffer[1] = regs[currentReg + 1];

  // If you want to send a single value larger than one byte, you will need to split it into bytes,
  // send and then reassemble at the master. Maximum size in the library default is 32 bytes
  // You can increase this in usitwislave.h
  uint16_t val = 256;
  *output_buffer_length = 2;
  output_buffer[0] = (val >> 8) & 0xFF;
  output_buffer[1] = val & 0xFF;
}

void idle() {
  // whatever you need to do, flash LEDs, etc.
}

int main() {
  usi_twi_slave(0x10, 0, request, nullptr);
  while (1) {};
}
```

## Use with PlatformIO
To use this library with PlatformIO, create your project then clone this library into `/lib/`. If you're writing C++, you will also need to rename `usitwislave.c` to `usitwislave.cpp`. Finally, include `#import "usitwisliave.h"` in your project.
```bash
cd lib/
git clone https://github.com/eriksl/usitwislave.git
cd usitwislave
mv usitwislave.c usitwislave.cpp
```
If you wish to use the USI on PortA with the ATtiny261/461/861 devices, edit `platform.ini` and add:
```
build_flags = -DUSI_ON_PORT_A
```

## Supported devices
* ATtiny24/44/84
* ATtiny25/45/85
* ATtiny26
* ATtiny261/461/861 (both B port and A port)
* ATtiny261A/461A/861A (both B port and A port)
* ATMega165/169
* ATMega325/3250/645/6540/329/3290

**Note** To use the USI on Port A of the ATtiny261/461/861 devices, include the `-DUSI_ON_PORT_A` flag (more below). These devices will use Port B by default.

## Differences to existing versions
Compared with the Atmel and David Blake versions:
* Actually works, slave can reply data (as opposed to the Atmel version).
* Completely different coding style to Donald Blake's 
version (higher level, but keeping same compiler output) you may or may not like that ;-)
* Compiles to a standalone library file (.a) and header (.h) that can be included into several projects independently. Please make sure you're using a version that was compiled for the device you're using (see Makefile).
* Added support for a few newer ATTiny devices
* Added support for USI bus on port A when using attiny*61 devices (this is a library compile time option though, because it MUST work with #defines). Add `-DUSI_ON_PORT_A` in the Makefile to the "CFLAGS" section to enable it.
* Last but not least: support for "complete" transactions, i.e. start-data-stop and act upon it, instead of waiting for the next start condition. Due to poor design of USI, the stop condition can only be polled, so the mcu needs to busy wait on the stop condition flag. I've found a way to only do busy polling when it is necessary, i.e. when a transaction is actually running. Otherwise, the mcu waits in sleep mode (if specified using use_sleep, see below).

I have found a few other situations that would cause mayhem and have been fixed:
* Bus lockup after the master has written to a non-existent slave address
* Slave would receive bytes not addressed to it when a master would address two or more slaves after each other, without sending a stop condition in between ("repeated start")

Probably there are still other bugs / conditions, which I haven't run into yet.
