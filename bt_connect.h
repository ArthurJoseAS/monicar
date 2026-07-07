#pragma once
#include <Arduino.h>
#include <LiquidCrystal.h>

extern LiquidCrystal lcd;

void setupBluetooth();
void checkBluetoothData();
bool isBleConnected();

float getEngineRPM();
int getVehicleSpeed();
int getCoolantTemperature();
float getFuelLevel();
float getInstantKmL();
float getAirIntake();
float getAverageKmL();