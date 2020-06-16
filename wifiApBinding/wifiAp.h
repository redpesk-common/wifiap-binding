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

#ifndef WIFI_AP_HEADER_FILE
#define WIFI_AP_HEADER_FILE

    // WiFi access point configuration file
    #define WIFI_HOSTAPD_FILE "/tmp/hostapd.conf"

    typedef enum
    {
         WIFI_AP_SECURITY_NONE = 0,
             ///< WiFi Access Point is open and has no password.

         WIFI_AP_SECURITY_WPA2 = 1
             ///< WiFi Access Point has WPA2 activated.
    }
    wifiAp_SecurityProtocol_t;

    struct event
    {
        struct event *next;
        struct afb_event event;
        char name[];
    };

    typedef struct wifiApT_{
        afb_api_t api;
        const char *ssid;
        const char passphrase[64];
        bool discoverable;
        wifiAp_SecurityProtocol_t securityProtocol;
        int SavedIeeeStdMask;
    }wifiApT;


#endif