#include <cmath>
#include "bt_connect.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>

//Ids extraídos com o celular do scanner OBD.
static BLEUUID serviceUUID("0000FFF0-0000-1000-8000-00805F9B34FB");
static BLEUUID    txUUID("0000FFF2-0000-1000-8000-00805F9B34FB");  
static BLEUUID    rxUUID("0000FFF1-0000-1000-8000-00805F9B34FB");

static BLERemoteCharacteristic* pTxCharacteristic = nullptr;
static BLERemoteCharacteristic* pRxCharacteristic = nullptr;
static BLEClient* pClient = nullptr;

static uint8_t obdAddress[6] = {0xEE, 0x52, 0xD9, 0x0F, 0xB4, 0x7C};  //endereço MAC do scanner OBD utilizado
static bool connected = false;
static String bleBuffer = "";

static float lastKnownRPM = 0;
static int lastKnownSpeed = 0;
static int lastKnownCoolant = 0;
static float lastKnownFuelLevel = 0.0;
static float lastKnownMAF = 0.0;
static float instantKmL = 0.0;

static unsigned long lastQueryTime = 0;

static float discoveredDisplacement = 1.3;
static bool displacementFetched = false;
static int displacementAttempts = 0;

static float averageKmL = 0.0;
static float totalFuelConsumed = 0.0;     
static float totalDistanceTraveled = 0.0;
static unsigned long lastFuelCalcTime = 0;

static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
  for (size_t i = 0; i < length; i++) {
    bleBuffer += (char)pData[i];
  }
}

bool isBleConnected() {
  return connected;
}


void sendOBDCommand(String command) {
  if (connected && pTxCharacteristic != nullptr) {
    Serial.print("[TX]: ");
    Serial.println(command);
    pTxCharacteristic->writeValue(command + "\r", true);
  }
}

void setupBluetooth() {
  lcd.clear();
  lcd.print("INICIANDO BT");
  BLEDevice::init("ESP32_S3_OBD");
  bleBuffer.reserve(64);
  pClient = BLEDevice::createClient();
  
  lcd.clear();
  lcd.print("CONECTANDO OBDII");
  if (!pClient->connect(BLEAddress(obdAddress))) {
    lcd.clear();
    lcd.print("Conexao Falhou!");
    connected = false;
    return;
  }
  
  lcd.clear();
  lcd.print("Checking UUID...");
  BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    lcd.clear();
    lcd.print("UUID Missing!");
    pClient->disconnect();
    connected = false;
    return;
  }
  
  pTxCharacteristic = pRemoteService->getCharacteristic(txUUID);
  pRxCharacteristic = pRemoteService->getCharacteristic(rxUUID);
  
  if (pTxCharacteristic == nullptr || pRxCharacteristic == nullptr) {
    pClient->disconnect();
    connected = false;
    return;
  }
  
  if(pRxCharacteristic->canNotify()) {
    pRxCharacteristic->registerForNotify(notifyCallback);
  }
  
  connected = true;
  lcd.clear();
  lcd.print("Conectado!");
  delay(1000);
  
  sendOBDCommand("ATZ"); 
  delay(500);
  sendOBDCommand("ATSP0");
  delay(500);
}
bool parseEngineRPM(String cleanBuffer) {
  int idx = cleanBuffer.indexOf("41 0C");
  if (idx == -1) return false;

  String payload = cleanBuffer.substring(idx + 5);
  payload.replace(" ", ""); 
  
  if (payload.length() < 4) return false;

  long valA = strtol(payload.substring(0, 2).c_str(), NULL, 16);
  long valB = strtol(payload.substring(2, 4).c_str(), NULL, 16);
  
  lastKnownRPM = ((valA * 256) + valB) / 4.0;
  return true;
}

bool parseSpeed(String cleanBuffer) {
  int idx = cleanBuffer.indexOf("41 0D");
  if (idx == -1) return false;

  String payload = cleanBuffer.substring(idx + 5);
  payload.replace(" ", "");
  
  if (payload.length() < 2) return false;

  long valA = strtol(payload.substring(0, 2).c_str(), NULL, 16);
  if (valA >= 0) {
    lastKnownSpeed = valA;
  }
  return true;
}

bool parseCoolant(String cleanBuffer) {
  int idx = cleanBuffer.indexOf("41 05"); 
  if (idx == -1) return false;

  String payload = cleanBuffer.substring(idx + 5);
  payload.replace(" ", "");
  
  if (payload.length() < 2) return false;

  lastKnownCoolant = strtol(payload.substring(0, 2).c_str(), NULL, 16) - 40;
  return true;
}

bool parseGas(String cleanBuffer) {
  int idx = cleanBuffer.indexOf("41 2F");
  if (idx == -1) return false;

  String payload = cleanBuffer.substring(idx + 5);
  payload.replace(" ", "");
  
  if (payload.length() < 2) return false;

  lastKnownFuelLevel = (strtol(payload.substring(0, 2).c_str(), NULL, 16) * 100.0) / 255.0;
  return true;
}

void calculateFuelEconomy() {
  if (lastKnownMAF <= 0.0) {
    instantKmL = INFINITY;
    return;
  }
  float airFuelRatio = 14.7;
  float gasolineDensity = 740.0;
  
  float litersPerHour = (lastKnownMAF * 3600.0) / (airFuelRatio * gasolineDensity);
  if (lastKnownSpeed > 0) {
    instantKmL = (float)lastKnownSpeed / litersPerHour;
  } else {
    instantKmL = 0.0; 
  }
}
void parseDisplacement(String cleanBuffer) {
  int idx = cleanBuffer.indexOf("49 0E");
  if (idx == -1) return;

  String payload = cleanBuffer.substring(idx + 5);
  payload.replace(" ", "");

  if (payload.length() < 4) return; 

  long valA = strtol(payload.substring(0, 2).c_str(), NULL, 16);
  long valB = strtol(payload.substring(2, 4).c_str(), NULL, 16);

  float calculatedSize = ((valA * 256.0) + valB) / 32.0;
  
  if (calculatedSize > 0.5 && calculatedSize < 8.0) {
    discoveredDisplacement = calculatedSize;
    Serial.print("--- Dynamic Engine Size Discovered: ");
    Serial.print(discoveredDisplacement);
    Serial.println("L ---");
  }
  
  displacementFetched = true;
}

bool parseIntake(String cleanBuffer) {
  int idx = cleanBuffer.indexOf("41 0B");
  if (idx == -1) return false;

  String payload = cleanBuffer.substring(idx + 5);
  payload.replace(" ", "");
  
  if (payload.length() < 2) return false;

  long rawMap = strtol(payload.substring(0, 2).c_str(), NULL, 16);
  
  if (rawMap >= 0) {
    lastKnownMAF = rawMap;

    if (lastKnownRPM > 200 && lastKnownSpeed > 0) {
      float calculatedAirMassFlow = (rawMap * lastKnownRPM * discoveredDisplacement * 0.80 * 28.97) / (8.314 * 308.15 * 120.0);
      float fuelFlowGramsPerSecond = calculatedAirMassFlow / 14.7;
      float litersPerHour = (fuelFlowGramsPerSecond / 740.0) * 3600.0;

      if (litersPerHour > 0.1) {
        instantKmL = (float)lastKnownSpeed / litersPerHour;
      } else {
        instantKmL = 0.0;
      }

      unsigned long currentTime = millis();
      
      if (lastFuelCalcTime > 0) { 
        float deltaTimeHours = (currentTime - lastFuelCalcTime) / 3600000.0;
        
        totalFuelConsumed += (litersPerHour * deltaTimeHours);
        totalDistanceTraveled += (lastKnownSpeed * deltaTimeHours);
        
        if (totalFuelConsumed > 0.001) { 
          averageKmL = totalDistanceTraveled / totalFuelConsumed;
        }
      }
      lastFuelCalcTime = currentTime;

    } else {
      instantKmL = 0.0; 
      lastFuelCalcTime = millis(); 
    }
  }
  return true;
}


void sendBulkObdCommands() {
  while (bleBuffer.indexOf('>') != -1) {
    
    int response_delimiter = bleBuffer.indexOf('>');
    
    String packet = bleBuffer.substring(0, response_delimiter);
    
    packet.replace("\r", " ");
    packet.replace("\n", " ");
    packet.trim();
    if(packet.length() > 0) {
      Serial.print("[RX]: ");
      Serial.println(packet);
    }
    if (packet.indexOf("41 0C") != -1) {
      parseEngineRPM(packet);
    } 
    if (packet.indexOf("41 0D") != -1) {
      parseSpeed(packet);
    } 
    if (packet.indexOf("41 05") != -1) {
      parseCoolant(packet);
    } 
    if (packet.indexOf("41 2F") != -1) {
      parseGas(packet);
    } 
    if (packet.indexOf("41 0B") != -1) {
      parseIntake(packet);
    }
    if (packet.indexOf("49 0E") != -1) {
      parseDisplacement(packet);
    }

    bleBuffer.remove(0, response_delimiter + 1);
  }
}

void checkBluetoothData() {
  if (!connected) return;

  if (!displacementFetched && displacementAttempts < 5) {
    static unsigned long lastFetchAttempt = 0;
    if (millis() - lastFetchAttempt > 1000) { 
      lastFetchAttempt = millis();
      bleBuffer = "";
      sendOBDCommand("090E"); 
      displacementAttempts++;
    }
  }
  else {
    if (millis() - lastQueryTime > 150) {
      lastQueryTime = millis();
      
      static int queryState = 0;
      bleBuffer = ""; 
      
      switch (queryState) {
        case 0:
          sendOBDCommand("010C"); // RPM
          break;
        case 1:
          sendOBDCommand("010D"); // SPEED
          break;
        case 2:
          sendOBDCommand("0105"); // COOLANT
          break;
        case 3:
          sendOBDCommand("012F"); // GAS
          break;
        case 4:
          sendOBDCommand("010B"); // MASS AIR PRESSURE
          break;
      }
      
      queryState = (queryState + 1) % 5; 
    }
  }
  
  if (bleBuffer.length() > 0) {
    if (bleBuffer.indexOf('>') != -1) {
      sendBulkObdCommands();
    }
  }
}


float getEngineRPM() {
  return lastKnownRPM;
}

int getVehicleSpeed() {
  return lastKnownSpeed;
}

int getCoolantTemperature() {
  return lastKnownCoolant;
}

float getFuelLevel() {
  return lastKnownFuelLevel;
}

float getAirIntake(){
  return lastKnownMAF;
}

float getInstantKmL() {
  return instantKmL;
}
float getAverageKmL() {
  return averageKmL;
}