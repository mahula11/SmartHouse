
#include <Arduino.h>

#include "device.h"

Device * Device::s_instance = nullptr;
//ArrivedConfiguration Device::s_arrivedConf;
MCP_CAN Device::s_can(10);
Configuration Device::s_conf;
SimpleFIFO<ST_CANBUS_RECEIVED_DATA, 10> Device::s_receivedCanBusData;
ArrivedConfiguration * Device::s_arrivedConf = nullptr;
//volatile bool Device::s_newModifiedIsSet = false;
//uint16_t Device::s_deviceAddress = 0;
//bool Device::s_firstConfMessage = true;
//byte Device::s_numberOfMsgFromConf = 0;
//byte Device::s_numberOfArrivedMsg = 0;

Device::Device() {
	s_instance = this;
}


Device::~Device() {
}

void Device::setPins() {
	for (int i = 0; i < s_conf.getCount(); i++) {
		switch (s_conf.getConf(i)->getType()) {
			case DEVICE_TYPE_LIGHT: {
				pinMode(((CConfMsg_light*)s_conf.getConf(i))->_gpio, OUTPUT);
				CTrafficMsg_askSwitchForData asfd(((CConfMsg_light*)s_conf.getConf(i))->_switchMacID, ((CConfMsg_light*)s_conf.getConf(i))->_switchGPIO);
				//sendRequest_askSwitchForValue(((CConfMsg_light*)s_conf.getConf(i))->_switchMacID, ((CConfMsg_light*)s_conf.getConf(i))->_switchGPIO);
				sendMsg(asfd);
			}
				break;
			//case DEVICE_TYPE_SOCKET:
			//	//pinMode(CanExt::getLightGPIO(pData), OUTPUT);
			//	break;
			
			//case DEVICE_TYPE_PUSH_BUTTON:
			//case DEVICE_TYPE_STAIR_CASE_SWITCH:
			//	break;
			case DEVICE_TYPE_SWITCH:
				pinMode(((CConfMsg_switch*)s_conf.getConf(i))->_gpio, INPUT);
				break;
		}
	}
}

void Device::checkModifiedData(CDataBase * pConfData, byte index) {
	//* tato modifikacia je nastavena vtedy, ked pridu nove data cez CanBus 
	//* napr prepnutie vypinaca vysle spravu s novou hodnotou pre ziarovky, ktore pocuvaju pre dany vypinac
	if (s_conf.getConfValue(index)->_modified) {
		switch (pConfData->getType()) {
			case DEVICE_TYPE_LIGHT:
				DEBUG("Set light on PIN:" << ((CConfMsg_light*)pConfData)->_gpio << " value:" << s_conf.getConfValue(index)->_value);
				digitalWrite(((CConfMsg_light*)pConfData)->_gpio, s_conf.getConfValue(index)->_value);
				break;
			//case DEVICE_TYPE_LIGHT_WITH_DIMMER:
			//	break;
		}
		s_conf.getConfValue(index)->_modified = false;
	}
}

void Device::checkValueOnPins(CDataBase * pConfData, byte index) {
	switch (pConfData->getType()) {
		//case DEVICE_TYPE_PUSH_BUTTON:
		//case DEVICE_TYPE_STAIR_CASE_SWITCH:
		//	break;
		case DEVICE_TYPE_SWITCH:
		{
			byte pinValue = digitalRead(((CConfMsg_switch*)pConfData)->_gpio);			
			//* if values are different, then send message to the lights
			if (pinValue != s_conf.getConfValue(index)->_value) {
				DEBUG(VAR(pinValue));
				DEBUG(F("_value:") << s_conf.getConfValue(index)->_value);

				//* set value without modify flag
				s_conf.setConfValue(index, pinValue, false);				

				//sendRequest_fromSwitch(((CConfMsg_switch*)pConfData)->_gpio, pinValue);
				CTrafficMsg_fromSwitch msgFromSwitch(s_conf.getMacAddress(), ((CConfMsg_switch*)pConfData)->_gpio, pinValue);
				sendMsg(msgFromSwitch);			
			}
		}
		break;
	}
}

void Device::detectCanBusSpeed() {
	int8_t iSpeed;
	CTrafficMsg_ping ping(s_conf.getMacAddress());
	DEBUG(F("Going to detect canbus speed"));

	//* looking for other devices through CAN BUS speed
	//* sent message must be received by someone
	while (1) {
		for (iSpeed = 0; iSpeed < CANBUS__COUNT_OF_SPEEDS; iSpeed++) {
			//* inicialize CAN BUS
			if (s_can.begin(MCP_ANY, iSpeed, MCP_8MHZ) != CAN_OK) {
				//* some speeds could not be available
				DEBUG(F("Error Initializing MCP2515...") << VAR(iSpeed));
				continue;
			}
			s_can.setMode(MCP_NORMAL);

			//* try ping message
			if (sendMsg(ping) == CAN_OK) {
				DEBUG(F("Speed ") << canBusSpeeds[iSpeed] << ": 1");
				//* someoone received message, so we found speed
				break;
			} else {
				DEBUG(F("Speed ") << canBusSpeeds[iSpeed] << ": 0");
			}
			delay(50);
		}
		if (iSpeed != CANBUS__COUNT_OF_SPEEDS) {
			DEBUG(F("Speed was founded:") << canBusSpeeds[iSpeed]);
			//* speed was founded
			EEPROM.writeByte(EEPROM_ADDRESS__CAN_BUS_SPEED, iSpeed);
			doReset();
		}
		//* check for speeds every second
		delay(1000);
	}
}

void Device::iniCanBus(uint8_t canBusSpeed) {
	if (canBusSpeed == CANBUS__DETECT_SPEED) {
		//* going to detect canbus speed
		detectCanBusSpeed();
	} else {
		if (s_can.begin(MCP_ANY, canBusSpeed, MCP_8MHZ) == CAN_OK) {
			DEBUG(F("MCP2515 Success:") << canBusSpeeds[canBusSpeed]);
		} else {
			DEBUG(F("MCP2515 Fail"));
		}
		// Change to normal mode to allow messages to be transmitted
		s_can.setMode(MCP_NORMAL);

		//* send message Im Up
		CTrafficMsg_ImUp msgImUp(s_conf.getMacAddress());
		if (sendMsg(msgImUp) == CAN_FAIL) {
			//* if message no one received, then maybe this device is first after restart of all devices, 
			//* so now it will be sending every second ping message
			//* after 6 times (6 seconds) going to detect CAN BUS speed, maybe other devices are on different speed
			uint8_t iPing;
			CTrafficMsg_ping msgPing(s_conf.getMacAddress());
			DEBUG(F("Trying to ping some device(6x)"));
			for (iPing = 0; iPing < 6; iPing++) {
				//* delay 1 second for repeating ping
				delay(1000);
				if (sendMsg(msgPing) == CAN_OK) {
					break;
				}
			}
			if (iPing == 6) {
				//* going to detect canbus speed
				detectCanBusSpeed();
			}
		}
	}
}

void Device::init() {
	s_arrivedConf = nullptr;

	

	//aa a1;
	//a1.canid = 22; 
	//int a;
	//a = 1;

	//SimpleFIFO<aa, 10> sFIFO;
	//sFIFO.push(a1);

	s_conf.setConfigurationStatic(eepromConf.getCountOfConf(),
		eepromConf.getWatchdogTimeout(),
		eepromConf.getAutoResetTime(),
		eepromConf.getMacAddress(),
		eepromConf.getCanBusSpeed());

	//* inicialize canBus
	iniCanBus(s_conf.getCanBusSpeed());

	pinMode(CAN0_INT, INPUT);   // Configuring pin for /INT input
	randomSeed(analogRead(0));

	attachInterrupt(digitalPinToInterrupt(CAN0_INT), interruptFromCanBus, FALLING);

	//* skontroluje, ci mame konfiguracne spravy. pokial nie, tak treba poziadat o konfiguraciu
	if (eepromConf.getCountOfConf()) {
		//* nacitaj conf
		DEBUG(F("Set conf from eeprom.") << s_conf.getMacAddress());
		s_conf.setConfiguration(eepromConf.readConf());
		
		//CTrafficMsg_ImUp imUpMsg(s_conf.getMacAddress());
		//sendMsg(imUpMsg);


		setPins();
	} else {
		//* pocet je 0, takze ziadnu konfiguraciu v eeprom nemame, treba poziadat o novu.
		//sendRequest_forConfiguration();
		DEBUG(F("Device will ask for conf."));
		CConfMsg_askForConfiguration afc(s_conf.getMacAddress());
		sendMsg(afc);
	}
	DEBUG(F("Init is complet!"));
}

void Device::update() {
	//* vynuteny restart
	//* pokial je millis() mensi ako nastaveny cas (4 hodiny), dovtedy sa watchdog bude resetovat. 
	//* pokial tento cas presiahne, tak nedovolime reset watchdogu a tym bude vynuteny reset procesoru
	if (s_conf.getAutoResetTime() == 0 || millis() < s_conf.getAutoResetTime()) {
		//* resetuje watchdog, zabrani restartu
		wdt_reset();
	}
		//* test watchdog timeouts, set millis in delay higher then watchdog timer, processor will be reseting around
		//DEBUG("po wdt_resete");
		//delay(2500);
	//* s_arrivedConf is read in interruptFromCanBus
	//* if arrived configuration is complet, then copy it to eeprom
	if (s_arrivedConf && s_arrivedConf->isComplet()) {		
		DEBUG(F("New conf will be processed"));
		//* zapiseme do eeprom
		eepromConf.writeConf(s_arrivedConf->getCount(), s_arrivedConf->getConf());
		//DEBUG(F("Processor reset"));
		//* do reset and do not have to clean and set up configuration
		doReset();
		////* zapiseme do Configuration
		//s_conf.setConfiguration(eepromConf.readConf());
		//setPins();
		////* zmazeme _arriveConf
		//delete s_arrivedConf;
		//s_arrivedConf = nullptr;
	}

	for (int index = 0; index < s_conf.getCount(); index++) {
		//ConfData * pData = & s_conf.getConf(index)->_confData;
		CDataBase * pConfData = s_conf.getConf(index);
		//* have a look for new arrived data from configuration
		checkModifiedData(pConfData, index);
		//* process changed values from GPIO
		checkValueOnPins(pConfData, index);
	}

#ifdef DEBUG_BUILD
	//* those is only for testing
	if (Serial.available()) {
		int incomingByte = Serial.read();
		Serial.print(F("I received: "));
		Serial.println(incomingByte, DEC);
		switch (incomingByte) {
			case 'r': {
				//* set to zero conter of configurations
				//* this enforce new conf
				EEPROM.writeByte(EEPROM_ADDRESS__CONF_COUNT, 0);
				//* do reset
				doReset();
				break;
			}
			case 'g': {
				//* inicialize canBus
				iniCanBus(CAN_500KBPS);
				break;
			}
			case 'h': {
				//* inicialize canBus
				iniCanBus(CAN_100KBPS);
				break;
			}
			case 'j': {
				//* inicialize canBus
				iniCanBus(CAN_1000KBPS);
				break;
			}
		}
	}
#endif
}

void Device::doReset(uint16_t upperBoundOfRandomTime) {
	DEBUG(F("Processor reset"));
	if (upperBoundOfRandomTime) {	
		long rand = random(0, upperBoundOfRandomTime);
		DEBUG(F("Reset in ") << rand << F("ms"));
		delay(rand);
	}
	Serial.flush();
	wdt_enable(WDTO_15MS);
	delay(100000000);
	//delayMicroseconds(10000);
}

#ifdef DEBUG_BUILD
	unsigned long gCounter = 0;
#endif

void Device::interruptFromCanBus() {
	byte len;
	ST_CANBUS_RECEIVED_DATA stData;
	CanID canID;
	while (s_can.readMsgBuf(&stData.canID, &len, stData.data) == CAN_OK) {
		canID._canID = stData.canID;
		if (canID.getMacID() == s_conf.getMacAddress() || canID.getMacID() == CANBUS__MESSAGE_TO_ALL) {
			//* push to the queue only messages for relevant MacID or messages to all
			s_receivedCanBusData.push(stData);
		}		
	}
}

void Device::test() {
	//ST_CANBUS_RECEIVED_DATA stData;
	//s_receivedCanBusData.push(stData);
	//* ----------------------------

	//s_can.setMode(MCP_NORMAL);
	//Device * instance = getInstance();
	#ifdef DEBUG_BUILD
		unsigned long counter = gCounter++;
	#endif
	DEBUG(F("-----------------------") << endl 
		<< F("IntFromCanBusStart:") << counter 
		<< F(",MacID:") << s_conf.getMacAddress() 
		<< F(",milis:") << millis());
	CanID canId;
	byte len = 0;
	MsgData rxBuf;
	uint8_t readMsgStat;
	while ((readMsgStat = s_can.readMsgBuf(&canId._canID, &len, rxBuf)) == CAN_OK) {
		DEBUG(F("Received:\n CanID:") << canId._canID << F(",MacID:") << canId.getMacID()
			<< F(",\n fromConf:") << canId.hasFlag_fromConfiguration()
			<< F(",\n fromSwitch:") << canId.hasFlag_fromSwitch()
			<< F(",\n askSwitchForVal:") << canId.hasFlag_askSwitchForValue()
			<< F(",\n ping:") << canId.hasFlag_ping() 
			<< F(",\n ImUp:") << canId.hasFlag_ImUp());

		if (canId.hasFlag_fromConfiguration() && canId.getMacID() == CANBUS__MESSAGE_TO_ALL) {
			//* set new CAN BUS speed
			if (canId.hasFlag_fromConfSetCanBusSpeed()) {
				DEBUG(F("Set CAN BUS speed to:") << rxBuf[0]);
				EEPROM.writeByte(EEPROM_ADDRESS__CAN_BUS_SPEED, (uint8_t)rxBuf[0]);
				doReset(1000);
			}
		}

		if (canId.hasFlag_fromConfiguration() && canId.getMacID() == s_conf.getMacAddress()) {
			//* messages from configuration server
			if (s_arrivedConf == nullptr) {
				s_arrivedConf = new ArrivedConfiguration();
			}
			
			//* sprava moze prist z FE, bez vyziadania
			//* nastavime timeout pre watchdog
			if (canId.hasFlag_fromConfSetWatchdog()) {
				DEBUG(F("Set watchdog message, val:") << rxBuf[0]);
				eepromConf.setWatchdogTimeout((WATCHDOG_TIMEOUT)rxBuf[0]);
				continue;
			}

			//* received reset from conf
			//* disable wdt_reset
			if (canId.hasFlag_fromConfReset()) {
				DEBUG(F("Reset message!"));
				doReset();
			}

			if (canId.hasFlag_fromConfAutoResetTime()) {
				DEBUG(F("AutoReset message, val:") << rxBuf[0]);
				eepromConf.setAutoResetTime(rxBuf[0]);
				s_conf.setAutoResetTime(rxBuf[0]);
				continue;
			}

			//* ked pride prva konfiguracna sprava, tak v datach, v prvom byte mame pocet sprav, ktore este pridu
			//* getCount vrati nulu, pretoze este neviemme pocet sprav
			if (s_arrivedConf->getCount()) {
				byte type = canId.getConfigPart();
				DEBUG(F("Conf arrived for type:") << type);
				CDataBase * pConfData;
				switch (type) {
					case DEVICE_TYPE_SWITCH:
						pConfData = new CConfMsg_switch(rxBuf);
						break;
					case DEVICE_TYPE_LIGHT:
						pConfData = new CConfMsg_light(rxBuf);
						break;
				}
				s_arrivedConf->addConf(pConfData);
			} else {
				//* prisla prva sprava, prislo cislo, ktore je pocet sprav, ktore este pridu z CanConf
				DEBUG(F("Number of confs will arrive:") << rxBuf[0]);
				s_arrivedConf->setCount(rxBuf[0]);
			}
		} else if (canId.hasFlag_fromSwitch()) { //* message from switch to lights
			DEBUG(F("Messsage from switch arrived"));
			//* teraz skontrolovat ci ID vypinaca patri niektoremu vypinacu v nasej konfiguracii (pre niektoru ziarovku)
			CTrafficMsg_fromSwitch switchData(rxBuf);
			for (byte i = 0; i < s_conf.getCount(); i++) {
				//* vyhladavame len typ "ziarovky" a potom ich IDcka vypinacov
				//* 0 - typ (ziarovka), 1 - gpio, 2 - id vypinaca
				CDataBase * pData = s_conf.getConf(i);
				if (pData->getType() == DEVICE_TYPE_LIGHT && 
					((CConfMsg_light*)pData)->_switchMacID == canId.getMacID() && 
					((CConfMsg_light*)pData)->_switchGPIO == switchData._gpio) 
				{
					//s_newModifiedIsSet = true;
					s_conf.setConfValue(i, switchData._value, true);
					DEBUG(F("Nastavit hodnotu:") << switchData._value << F(" pre switch z MacID:") << canId.getMacID() 
						<< F(" a gpio:") << switchData._gpio);
				}
			}
		} else if (canId.hasFlag_askSwitchForValue() && canId.getMacID() == s_conf.getMacAddress()) {
			DEBUG(F("Message from light, asking switch for value"));

			byte gpio = rxBuf[0];
			byte pinValue = digitalRead(gpio);
			//sendRequest_fromSwitch(gpio, pinValue);
			CTrafficMsg_fromSwitch msgFromSwitch(s_conf.getMacAddress(), gpio, pinValue);
			sendMsg(msgFromSwitch);

		}
	}
	DEBUG(F("IntFromCanBusEnd:") << counter 
		<< F(",milis:") << millis() 
		<< F(",") << VAR(readMsgStat) << endl 
		<< F("-----------------------"));
}

//INT8U Device::sendMsgBuf(INT32U id, INT8U ext, INT8U len, INT8U *buf) {
//	INT8U res;
//
//	s_can.setMsg(id, 0, ext, len, buf);
//	res = s_can.sendMsg();
//
//	return res;
//}

uint8_t Device::sendMsg(CDataBase & cdb) {
	byte data[8];
	cdb.serialize(data);
	uint8_t ret;
	#ifdef DEBUG_BUILD
	static uint32_t counter = 0;
	#endif
	
	//if (s_can.setMode(MCP_NORMAL) == MCP2515_OK) {
	//	DEBUG(F("1.Set mode MCP_NORMAL succesful"));
	//} else {
	//	DEBUG(F("1.Set mode MCP_NORMAL fail"));
	//}
	DEBUG(F("Sending msg to CanBus:\n CanID:") << cdb._destCanID._canID
		<< F(",MacID:") << cdb._destCanID.getMacID()
		<< F(",\n deviceType:") << cdb.getType());
	//ret = sendMsgBuf(cdb._destCanID._canID, 1, cdb.getSize(), data);
	s_can.setMsg(cdb._destCanID._canID, 0, 1, cdb.getSize(), data);
	ret = s_can.sendMsg();
	if (ret == CAN_OK) {
		DEBUG(F("Msg was sent to CanBus:") << ++counter);
		return CAN_OK;
	} else {
		DEBUG(F("Failure when send to CanBus:\n CanID:") << cdb._destCanID._canID << F(",error:") << ret);
		return CAN_FAIL;
	}


//	if (1) {
//		//s_can.begin();
//		//delay(1000);
//		//s_can.mcp2515_reset();
//		if (s_can.setMode(MCP_LOOPBACK) == MCP2515_OK) {
//			DEBUG(F("2.Set mode MCP_LOOPBACK succesful"));
//		} else {
//			DEBUG(F("2.Set mode MCP_LOOPBACK fail"));
//		}
//		//s_can.setMode(MCP_NORMAL);
//		DEBUG(F("Sending msg to loopback:\n CanID:") << cdb._destCanID._canID
//			<< F(",MacID:") << cdb._destCanID.getMacID()
//			<< F(",\n deviceType:") << cdb.getType());
//		//DEBUG(F("---------------------send1"));
//		//ret = s_can.sendMsgBuf(cdb._destCanID._canID, cdb.getSize(), data);
//		ret = sendMsgBuf(cdb._destCanID._canID, 1, cdb.getSize(), data);
//		//DEBUG(F("---------------------send2"));
//#ifdef DEBUG_BUILD
//		if (ret == CAN_OK) {
//			DEBUG(F("Msg was sent to loopback"));
//		} else {
//			DEBUG(F("Failure when send over loopback:\n CanID:") << cdb._destCanID._canID << F(",error:") << ret);
//		}
//#endif
//		//DEBUG(F("Set back normal mode and send again message to CanBus"));
//		//s_can.setMode(MCP_NORMAL);
//	}
//	//delay(1000);
//	//s_can.mcp2515_reset();
//	if (s_can.setMode(MCP_NORMAL) == MCP2515_OK) {
//		DEBUG(F("3.Set mode MCP_NORMAL succesful"));
//	} else {
//		DEBUG(F("3.Set mode MCP_NORMAL fail"));
//	}
}
