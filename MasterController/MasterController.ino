#include <Wire.h>
#include <errno.h>
#include "SerialCommand.h" // SerialCommand by Steven Cogswell and Stefan Rado

#define MAX_SPLIT_FLAPS 16

SerialCommand cmd;

unsigned long prev_millis;

uint8_t splitflaps[MAX_SPLIT_FLAPS] {};
uint8_t n_splitflaps = 0;

// Console commands: 

// " TEXT

//   ",HELLO        type HELLO from current position
//   ",\014SPLIT\r"	re-home all positions and display HELLO, then set cursor position to 0

// a addr          assign address (only valid when there is a single display connected)
//   a 0x11	set display i2c address = 0x11

// z addr offset   set homing offset
//   z 0x11 3      display at 0x11 homes to position 3

// Text special chars

// 12 0xc 014	FF	re-home all positions
// 10 0xa 012	LF      clear line (wind up to space without homing)
// 13 0xd 015      CR      set position to 0
// 8  0x8 010      BS      backspace
// [A-Z,0-9,\- ]           display char, advance cursor position

static uint8_t response_buf[16];

void printhex(uint8_t val)
{
    if (val < 16) Serial.print('0');
    Serial.print(val, 16);

}

void hexdump(const uint8_t * buf, uint8_t len)
{
    for (uint16_t i = 0; i < len; ++i) {
        printhex(buf[i]);
        Serial.print(' ');
    }
}

int8_t check_display(uint8_t addr)
{
    Wire.beginTransmission(addr);
    Wire.write('q');
    Wire.endTransmission();

    Wire.requestFrom((int)addr, 7); // expect response S F D aa zz pp ss

    uint8_t i = 0;
    while (Wire.available() && i < sizeof(response_buf)) {
        response_buf[i++] = Wire.read();
    }

    hexdump(response_buf, i);

    if (i < 7) {
        return -1;
    }
    if (memcmp_P(response_buf, PSTR("SFD"), 3) == 0) {
        uint8_t addr = response_buf[3];
        int8_t zero = static_cast<int8_t>(response_buf[4]);
        uint8_t pos = response_buf[5];
        uint8_t state = response_buf[6];
        Serial.print(F("SFD 0x")); Serial.print(addr, 16); 
        if (zero >= 0) Serial.print('+'); 
        Serial.print(zero, 10); Serial.print('@'); Serial.print(pos);
        Serial.print('s'); Serial.print(state);

        for (auto i = 0; i < MAX_SPLIT_FLAPS; ++i) {
            if (splitflaps[i] == addr) break;
            if (splitflaps[i] == 0) {
                splitflaps[i] = addr;
            }
        }

        return 0;
    }

    return -1;
}

void scan_i2c()
{
    memset(splitflaps, 0, sizeof(splitflaps));
    n_splitflaps = 0;

    Serial.println(F("Scanning..."));
    int nDevices = 0;
    for (byte address = 1; address < 127; ++address) {
        // The i2c_scanner uses the return value of
        // the Wire.endTransmission to see if
        // a device did acknowledge to the address.
        Wire.beginTransmission(address);
        byte error = Wire.endTransmission();

        if (error == 0) {
            Serial.print(F("I2C device found at address 0x"));
            if (address < 16) {
                Serial.print('0');
            }
            Serial.print(address, HEX);
            Serial.print(F(": "));

            if (check_display(address) == 0) {
                n_splitflaps++;
            }
            else {
                Serial.print(F(" ERROR"));
            }

            Serial.println();

            ++nDevices;
        } 
        else if (error == 4) {
            Serial.print(F("Unknown error at address 0x"));
            if (address < 16) {
                Serial.print('0');
            }
            Serial.println(address, HEX);
        }
    }
    if (nDevices == 0) {
        Serial.println(F("No I2C devices found\n"));
    } else {
        Serial.println(F("done\n"));
    }

    Serial.print(F("Found split-flap displays: "));
    hexdump(splitflaps, n_splitflaps);
    Serial.println();
}

void sendAck()
{
    Serial.println(F("OK"));
}

void sendNak()
{
    Serial.println(F("ERROR"));
}

void cmdPrint()
{
    sendNak();
}

void cmdAssignAddress()
{
    if (n_splitflaps != 1) {
        Serial.print(F("address can be set only when there is a single display on the bus; now: ")); Serial.println(n_splitflaps);
        sendNak();
        return;
    }

    const char *arg1 = cmd.next();
    if (arg1 != nullptr) {
        errno = 0;
        int valor = strtol(arg1, nullptr, 0);
        if (errno == 0 && valor > 0 && valor < 128) {
            uint8_t addr = static_cast<uint8_t>(valor);
            Serial.print(F("set address to 0x")); printhex(addr); Serial.println();
            Wire.beginTransmission(splitflaps[0]);
            Wire.write('a');
            Wire.write(addr);
            Wire.write(static_cast<uint8_t>(~addr));
            Wire.endTransmission();
            sendAck();
            scan_i2c();
            return;
        }
    }

    sendNak();
}

void cmdSetHomingOffset()
{
    uint8_t addr = 255;
    int8_t offset = 127;
    const char *arg = cmd.next();
    if (arg != nullptr) {
        errno = 0;
        int valor = strtol(arg, nullptr, 0);
        if (errno == 0 && valor > 0 && valor < 128) {
            addr = valor;
        }
    }

    arg = cmd.next();
    if (arg != nullptr) {
        errno = 0;
        int valor = strtol(arg, nullptr, 0);
        if (errno == 0 && valor > -38 && valor < 38) {
            offset = valor;
        }
    }

    if (addr == 255 || offset == 127) {
        sendNak();
        return;
    }

    for (uint8_t i = 0; i < n_splitflaps; ++i) {
        if (splitflaps[i] == addr) {
            Wire.beginTransmission(addr);
            Wire.write('z');
            Wire.write(static_cast<uint8_t>(offset));
            Wire.endTransmission();
            sendAck();
            return;
        }
    }

    sendNak();
}

void cmdVersion()
{
    Serial.println(F("SFMC 0.1"));
    sendAck();
}

void cmdStatus()
{
    scan_i2c();
    sendAck();
}

void setup() {
    Wire.begin();
    Serial.begin(115200);

    cmd.addCommand("\"", cmdPrint); // print text on split-flaps
    cmd.addCommand("a", cmdAssignAddress); // assign address X to the only split-flap display on the bus
    cmd.addCommand("z", cmdSetHomingOffset); // set homing offset to split-flap display N = X
    cmd.addCommand("v", cmdVersion); // print version string
    cmd.addCommand("s", cmdStatus); // print status of every connected display

    prev_millis = millis();

    cmdVersion();
    delay(2000);
    cmdStatus();
}

void loop() {
    cmd.readSerial();
}
