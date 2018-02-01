/*
    *  Flybrix Flight Controller -- Copyright 2018 Flying Selfie Inc. d/b/a Flybrix
    *
    *  http://www.flybrix.com
*/

#include "serialFork.h"
#include <Arduino.h>
#include "board.h"
#include "devicename.h"
#include "usbModeSelector.h"

namespace {
struct USBComm {
    USBComm() {
        Serial.begin(9600);  // USB is always 12 Mbit/sec
    }

    bool read() {
        while (Serial.available()) {
            data_input.AppendToBuffer(Serial.read());
            if (data_input.IsDone())
                return true;
        }
        return false;
    }

    void write(uint8_t* data, size_t length) {
        Serial.write(data, length);
    }

    CobsReaderBuffer& buffer() {
        return data_input;
    }

   private:
    CobsReaderBuffer data_input;
};

USBComm usb_comm;

struct Bluetooth {
    Bluetooth() {
        pinMode(board::bluetooth::RESET, OUTPUT);
        digitalWrite(board::bluetooth::RESET, HIGH);
        Serial1.begin(57600);
    }

    void setBluetoothUart(const DeviceName& name);

    bool read() {
        while (Serial1.available()) {
            bytes_received++;
            data_input.AppendToBuffer(Serial1.read());
            if (data_input.IsDone())
                return true;
        }
        return false;
    }

    void write(uint8_t* data, size_t length) {
        bytes_sent += length;
        data_output.push(data, length);
    }

    void printStats(){
        Serial.printf("BT bytes sent/received: %d / %d \n", bytes_sent, bytes_received);
    }

    CobsReaderBuffer& buffer() {
        return data_input;
    }

    uint32_t flush(uint32_t times) {
        for (uint32_t i = 0; i < times; ++i) {
            size_t l{data_output.hasData()};
            if (!l) {
                return i;
            }
            Serial1.write(data_output.pop(), l);
        }
        return times;
    }

   private:
    struct BluetoothBuffer {
        size_t hasData() {
            size_t increment{(readerPointer <= writerPointer) ? (writerPointer - readerPointer) : (bufferSize - readerPointer)};
            if (increment > bufferChunk)
                return bufferChunk;
            return increment;
        }

        bool canFit(size_t size) {
            if (readerPointer <= writerPointer)
                return bufferSize - (writerPointer - readerPointer) > size;
            else
                return readerPointer - writerPointer > size;
        }

        const uint8_t* pop() {
            size_t outputPointer{readerPointer};
            readerPointer += hasData();
            if (readerPointer >= bufferSize)
                readerPointer -= bufferSize;
            return data + outputPointer;
        }

        void push(uint8_t* input_data, size_t length) {
            if (!canFit(length))  // The buffer is too full
                return;
            size_t lengthSubtraction{0};
            if (bufferSize - writerPointer < length)
                lengthSubtraction = length - (bufferSize - writerPointer);
            length -= lengthSubtraction;
            for (size_t pos = 0; pos < length; ++pos) {
                data[writerPointer++] = input_data[pos];
            }
            if (writerPointer == bufferSize) {
                writerPointer = 0;
                push(input_data + length, lengthSubtraction);
            }
        }

       private:
        static constexpr size_t bufferCount{100};
        static constexpr size_t bufferChunk{20};
        static constexpr size_t bufferSize{bufferCount * bufferChunk};
        uint8_t data[bufferSize];
        size_t writerPointer{0};
        size_t readerPointer{0};
    };

    size_t writerPosition{0};
    BluetoothBuffer data_output;
    CobsReaderBuffer data_input;
    uint32_t bytes_sent{0};
    uint32_t bytes_received{0};    
};

Bluetooth bluetooth;

void flushATmodeResponse() {
    // We can't ignore responses provided by AT mode
    delay(100);
    while (!Serial1.available()) {
    }
    while (Serial1.read() != '\n') {
    }
}

void Bluetooth::setBluetoothUart(const DeviceName& name) {
    // PIN 12 of teensy is BMD (P0.13)
    // PIN 30 of teensy is BMD (PO.14) AT Mode
    // PIN 28 of teensy is BMD RST
    // 20 - CTS P0.05
    // 0 - Rx P0.06
    // 6 - RTS P0.05
    // 1 - Tx
    pinMode(board::bluetooth::MODE, OUTPUT);
    pinMode(board::bluetooth::RESET, OUTPUT);
    digitalWriteFast(board::bluetooth::MODE, LOW);   // set AT mode
    digitalWriteFast(board::bluetooth::RESET, LOW);  // reset BMD
    delay(100);
    digitalWriteFast(board::bluetooth::RESET, HIGH);  // reset BMD complete, now in AT mode
    delay(2500);                                      // time needed initialization of AT mode

    Serial1.print("at$uen 01\n");
    flushATmodeResponse();

    Serial1.print("at$name ");
    Serial1.print(name.value);
    Serial1.print("\n");
    flushATmodeResponse();

    digitalWriteFast(board::bluetooth::MODE, HIGH);
    digitalWriteFast(board::bluetooth::RESET, LOW);  // reset BMD
    delay(100);
    digitalWriteFast(board::bluetooth::RESET, HIGH);  // reset BMD complete, now not in AT mode
}
}  // namespace

void setBluetoothUart(const DeviceName& name) {
    bluetooth.setBluetoothUart(name);
}

CobsReaderBuffer* readSerial() {
    if (usb_mode::get() == usb_mode::BLUETOOTH_MIRROR) {
        if (usb_comm.read())
            return &usb_comm.buffer();
    }
    if (bluetooth.read())
        return &bluetooth.buffer();
    return nullptr;
}

void writeSerial(uint8_t* data, size_t length) {
    if (usb_mode::get() == usb_mode::BLUETOOTH_MIRROR) {
        usb_comm.write(data, length);
    }
    bluetooth.write(data, length);
}

uint32_t flushSerial(uint32_t times) {
    return bluetooth.flush(times);
}

void printSerialReport() {
    bluetooth.printStats();
}
