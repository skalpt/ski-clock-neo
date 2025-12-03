#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

#include <Arduino.h>

void initDeviceConfig();

float getTemperatureOffset();
void setTemperatureOffset(float offset);

void handleConfigMessage(const String& message);

#endif
