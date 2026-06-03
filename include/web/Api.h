// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * SmallTV-Ultra Korean Custom Firmware
 * Copyright (C) 2026 Times-Z
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef API_H
#define API_H

#include "web/Webserver.h"

void setCorsHeaders(Webserver* webserver);
void registerApiEndpoints(Webserver* webserver);
void handleOtaUpload(Webserver* webserver, int mode);
void handleOtaFinished(Webserver* webserver);
void handleReboot(Webserver* webserver);
void handleFactoryReset(Webserver* webserver);
void handleSystemVersion(Webserver* webserver);
void handleSystemUpdateAvailableSet(Webserver* webserver);
void handleOtaStatus(Webserver* webserver);
void handleOtaCancel(Webserver* webserver);

void handleGifUpload(Webserver* webserver);
void handleListGifs(Webserver* webserver);
void handlePlayGif(Webserver* webserver);
void handleStopGif(Webserver* webserver);

void handleWifiScan(Webserver* webserver);
void handleWifiConnect(Webserver* webserver);
void handleWifiStatus(Webserver* webserver);

void handleNtpSync(Webserver* webserver);
void handleNtpStatus(Webserver* webserver);
void handleNtpConfigGet(Webserver* webserver);
void handleNtpConfigSet(Webserver* webserver);
void handleDisplayClockGet(Webserver* webserver);
void handleDisplayClockSet(Webserver* webserver);
void handleDisplayRotationGet(Webserver* webserver);
void handleDisplayRotationSet(Webserver* webserver);
void handleDisplayBrightnessGet(Webserver* webserver);
void handleDisplayBrightnessSet(Webserver* webserver);
void handleDisplayStartup(Webserver* webserver);

void handleLogsGet(Webserver* webserver);
void handleLogsDownload(Webserver* webserver);
void handleLogsClear(Webserver* webserver);
void handleWeatherConfigGet(Webserver* webserver);
void handleWeatherConfigSet(Webserver* webserver);
void handleWeatherStatusGet(Webserver* webserver);
void handleWeatherRefresh(Webserver* webserver);
void handleWeatherValidateKey(Webserver* webserver);

#endif  // API_H
