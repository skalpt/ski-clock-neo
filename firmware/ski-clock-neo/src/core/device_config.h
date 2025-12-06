#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

#include <Arduino.h>

void initDeviceConfig();

float getTemperatureOffset();
void setTemperatureOffset(float offset);

const char* getEnvironmentScope();
void setEnvironmentScope(const char* scope);

void handleConfigMessage(const String& message);

#endif
