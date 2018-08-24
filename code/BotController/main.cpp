#include "Arduino.h"
#include "MenuController.h"
#include "Engine.h"
#include "BotController.h"

#include <utilities/PatternBlinker.h>
#include <OmniWheel.h>

#define LED_PIN 13			// blinking LED on Teensy
static uint8_t DefaultPattern[3] = { 0b11001000, 0b00001100, 0b10000000 };	// nice!

PatternBlinker ledBlinker(LED_PIN, 50 /* ms */); // one bit in the patterns above is active for 100ms

HardwareSerial* logger = &Serial5;			// UART used to log
HardwareSerial* command = &Serial5;			// UART used to log

BotController botController;

void setup()
{
	ledBlinker.set(DefaultPattern,sizeof(DefaultPattern));

	command->begin(115200);
	botController.setup();

	command->println("BotController - h for help");
}

void loop()
{
	uint32_t now = millis();
	ledBlinker.loop(now);    	// LED on Teensy board and LED on power switch
	botController.loop();
}
