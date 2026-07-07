#include <LiquidCrystal.h>
#include "bt_connect.h"
#include "ELMduino.h"

// Pinos do LCD -> ESP32
const int rs = 20;
const int en = 21;
const int d4 = 2;
const int d5 = 42;
const int d6 = 41;
const int d7 = 40;

const int pinoBotao = 35;

LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

enum ScreenState {
  SCREEN_WELCOME,
  SCREEN_SCANNING,
  SCREEN_CONNECTED,        
  SCREEN_RPM_SPEED,        
  SCREEN_FUEL_INST_AVG,    
  SCREEN_COOLING_GAS_TANK  
};

ScreenState currentScreen = SCREEN_WELCOME;
bool lastButtonState = HIGH;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("--- ESP32-S3 Ligado com sucesso! ---");
  
  pinMode(pinoBotao, INPUT_PULLUP);
  
  Serial.println("Inicializando o Display LCD...");
  lcd.begin(16, 2);
  delay(100);
  lcd.clear();
  
  displayWelcomeScreen();
  Serial.println("Setup concluido com sucesso!");
}

void loop() {
  bool currentButtonState = digitalRead(pinoBotao);

  if (currentButtonState == LOW && lastButtonState == HIGH) {
    delay(50); 
    lcd.clear(); 
    
    switch (currentScreen) {
      case SCREEN_WELCOME:
        currentScreen = SCREEN_SCANNING;
        displayScanningScreen();
        setupBluetooth(); 
        break;
        
      case SCREEN_SCANNING:
      case SCREEN_CONNECTED:
        break;
        
      case SCREEN_RPM_SPEED:
        currentScreen = SCREEN_FUEL_INST_AVG;
        break;
        
      case SCREEN_FUEL_INST_AVG:
        currentScreen = SCREEN_COOLING_GAS_TANK;
        break;
        
      case SCREEN_COOLING_GAS_TANK:
        currentScreen = SCREEN_RPM_SPEED; 
        break;
    }
    delay(200); 
  }
  lastButtonState = currentButtonState;

  if (currentScreen == SCREEN_SCANNING) {
    animateScanningDots();
    checkBluetoothData();
    
    if (isBleConnected()) { 
      currentScreen = SCREEN_CONNECTED;
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("OBDII Conectado!");
      delay(1500);
      lcd.clear();
      currentScreen = SCREEN_RPM_SPEED; 
    }
  }
  else if (currentScreen >= SCREEN_RPM_SPEED) {
    checkBluetoothData(); 
    // Serial.print("RPM and speed: ");  
    // Serial.println(getEngineRPM());
    // Serial.println(getVehicleSpeed());
    // Serial.print("instcons and airintake: ");  
    // Serial.println(getInstantKmL());
    // Serial.println(getAirIntake());
    // Serial.print("temp and fuel: ");  
    // Serial.println(getCoolantTemperature());
    // Serial.println(getFuelLevel());
    switch (currentScreen) {
      case SCREEN_RPM_SPEED:
        updateRpmSpeedScreen();
        break;
      case SCREEN_FUEL_INST_AVG:
        updateFuelScreen();
        break;
      case SCREEN_COOLING_GAS_TANK:
        updateCoolingGasScreen();
        break;
      default:
        break;
    }
    delay(50); 
  }
}


void displayWelcomeScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Aperte o botao!");
  lcd.setCursor(0, 1);
  lcd.print("Conectar OBDII");
}

void displayScanningScreen() {
  Serial.println("Alternado para interface de escaneamento Bluetooth...");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Buscando OBDII...");
  lcd.setCursor(0, 1);
  lcd.print("Aguarde");
}

void animateScanningDots() {
  static unsigned long lastUpdate = 0;
  static int dotCount = 0;
  if (millis() - lastUpdate > 500) {
    lastUpdate = millis();
    dotCount = (dotCount + 1) % 4;
    lcd.setCursor(7, 1);
    switch(dotCount) {
      case 0: lcd.print("   "); break;
      case 1: lcd.print(".  "); break;
      case 2: lcd.print(".. "); break;
      case 3: lcd.print("..."); break;
    }
  }
}


void updateRpmSpeedScreen() {
  int rpm = (int)getEngineRPM();
  int speed = getVehicleSpeed();

  lcd.setCursor(0, 0);
  lcd.print("RPM: ");
  lcd.print(rpm);
  lcd.print("    "); 

  lcd.setCursor(0, 1);
  lcd.print("Veloc: ");
  lcd.print(speed);
  lcd.print(" Km/h   ");
}

void updateFuelScreen() {
  float instKmL = getInstantKmL();
  float avgKmL = getAverageKmL();

  lcd.setCursor(0, 0);
  lcd.print("Media: ");
  if (avgKmL > 0.0) {
    lcd.print(avgKmL, 1);
    lcd.print(" km/L  ");
  } else {
    lcd.print("--- km/L  ");
  }

  lcd.setCursor(0, 1);
  if (instKmL == INFINITY || isinf(instKmL) || instKmL > 999.0) {
    lcd.print("Inst : INF       ");
  } else {
    lcd.print("Inst : ");
    lcd.print(instKmL, 1); 
    lcd.print(" km/L  ");
  }
}

void updateCoolingGasScreen() {
  int temp = getCoolantTemperature();
  float gasPercent = getFuelLevel();

  lcd.setCursor(0, 0);
  lcd.print("Temp. Motor: ");
  
  lcd.setCursor(12, 0);
  lcd.print(temp);
  lcd.print((char)223); 
  lcd.print("C   ");

  lcd.setCursor(0, 1);
  lcd.print("Tanque: ");
  lcd.print((int)gasPercent);
  lcd.print("%     ");
}