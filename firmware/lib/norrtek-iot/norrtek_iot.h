#ifndef NORRTEK_IOT_H
#define NORRTEK_IOT_H

#include <Arduino.h>

#include "product_config.h"

#include "core/debug.h"
#include "core/device_info.h"
#include "core/timer_helpers.h"
#include "core/event_log.h"
#include "core/led_indicator.h"

#include "display/display_core.h"
#include "display/font_5x7.h"

#include "connectivity/wifi_config.h"
#include "connectivity/mqtt_client.h"
#include "connectivity/ota_update.h"

extern ProductConfig norrtekConfig;
extern DisplayUpdateCallback displayUpdateCallback;
extern MqttCommandCallback mqttCommandCallback;

void initNorrtekIoT(const ProductConfig& config);
void processNorrtekIoT();

void setDisplayUpdateCallback(DisplayUpdateCallback callback);
void setMqttCommandCallback(MqttCommandCallback callback);

const ProductConfig& getProductConfig();

#endif
