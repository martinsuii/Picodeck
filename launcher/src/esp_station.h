// ****************************************************************************
//
//          PicoDeck — "ESP Station" (ESP-NOW Rubber Ducky)
//
//   Sends a DuckyScript payload from the /payloads folder to a RubberDucky
//   station over ESP-NOW (via the ESP32 bridge), where it is typed on the
//   target host.
//
// ****************************************************************************

#ifndef _ESP_STATION_H
#define _ESP_STATION_H

#include "../include.h"

#ifdef __cplusplus
extern "C" {
#endif

void EspStationRun(void);

#ifdef __cplusplus
}
#endif

#endif // _ESP_STATION_H
