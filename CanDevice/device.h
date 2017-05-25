#pragma once

#include <Arduino.h>

#include <SPI.h>
#include <mcp_can.h>
#include <smartHouse.h>
#include <Streaming.h>

#include "configuration.h"
#include "arrivedConfiguration.h"
#include "eepromConf.h"

#define LOG_DEBUG(str) \
		Serial << str;

#define CAN0_INT 2   // Set INT to pin 2

class Device {
private:
	static MCP_CAN s_can;     // Set CS to pin 10 in constructor
	static ArrivedConfiguration * s_arrivedConf;
	//byte _data[8] = {0};
	//static uint16_t s_deviceAddress;
	//EepromConf & _eepromConf;
	static Configuration s_conf;

	//static bool s_firstConfMessage;
	//static byte s_numberOfMsgFromConf;
	//static byte s_numberOfArrivedMsg;

	static void interruptFromCanBus();
	void sendRequestForConfiguration();
	
	static Device * s_instance;
	static Device * getInstance() {
		return s_instance;
	}
public:
	Device();
	~Device();

	void init();
	void update();
	void setPinModes();
	void checkModifiedData(ConfData * pData, byte index);
	void checkValueOnPins(ConfData * pData, byte index);
};

extern EepromConf eepromConf;