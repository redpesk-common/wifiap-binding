/*
 * Copyright (C) 2016-2018 "IoT.bzh"
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "afb-helpers4/afb-req-utils.h"

#ifndef WIFI_AP_HEADER_FILE
#define WIFI_AP_HEADER_FILE

// Set of commands to drive the WiFi features.
#define COMMAND_WIFI_HW_START        " WIFI_START"
#define COMMAND_WIFI_HW_STOP         " WIFI_STOP"
#define COMMAND_WIFI_SET_EVENT       " WIFI_SET_EVENT"
#define COMMAND_WIFI_UNSET_EVENT     " WIFI_UNSET_EVENT"
#define COMMAND_WIFI_FIREWALLD_ALLOW " WIFI_FIREWALLD_ALLOW"
#define COMMAND_WIFI_NM_UNMANAGE     " WIFI_NM_UNMANAGE"
#define COMMAND_WIFIAP_HOSTAPD_START " WIFIAP_HOSTAPD_START"
#define COMMAND_WIFIAP_HOSTAPD_STOP  " WIFIAP_HOSTAPD_STOP"
#define COMMAND_WIFIAP_WLAN_UP       " WIFIAP_WLAN_UP"
#define COMMAND_DNSMASQ_RESTART      " DNSMASQ_RESTART"

#ifdef TEST_MODE
#define COMMAND_GET_VIRTUAL_INTERFACE_NAME "GET_VIRTUAL_INTERFACE_NAME"
#endif

#define MAX_IP_ADDRESS_LENGTH      15
#define WIFI_MAX_EVENT_INFO_LENGTH 512

#define HARDWARE_MODE_MASK 0x000F  // Hardware mode mask
#define PATH_MAX           8192

struct event
{
    struct event *next;
    afb_event_t event;
    char name[];
};

#endif