/*******************************************************************************
# Copyright 2020 IoT.bzh
#
# author: Salma Raiss <salma.raiss@iot.bzh>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
*******************************************************************************/

#ifndef WIFI_AP_DATA_HEADER_LIB_FILE
#define WIFI_AP_DATA_HEADER_LIB_FILE

#include <stdint.h>
#include <stdbool.h>
#include <urcu/list.h>

// SSID length definitions
#define MAX_SSID_LENGTH 32
#define MIN_SSID_LENGTH 1
#define MAX_SSID_BYTES  33

// access point channels definitions (default for 2.4Ghz defined in "wifiAp.h")
#define MIN_CHANNEL_VALUE_DEF 1  // for 2.4 Ghz wifi
#define MAX_CHANNEL_VALUE_DEF 14
#define MIN_CHANNEL_STD_A     7  // For 6 Ghz wifi
#define MAX_CHANNEL_STD_A     196
#define MIN_CHANNEL_STD_AD    1  // for 60 Ghz wifi
#define MAX_CHANNEL_STD_AD    6

// IEEE standard corresponding bitmasks
#define WIFI_AP_BITMASK_IEEE_STD_A  0x1  ///< IEEE 802.11a (5 GHz) Bit Mask
#define WIFI_AP_BITMASK_IEEE_STD_B  0x2  ///< IEEE 802.11b (2.4 GHz) Bit Mask
#define WIFI_AP_BITMASK_IEEE_STD_G  0x4  ///< IEEE 802.11g (2.4 GHz) Bit Mask
#define WIFI_AP_BITMASK_IEEE_STD_AD 0x8  ///< IEEE 802.11ad (60 GHz) Bit Mask
#define WIFI_AP_BITMASK_IEEE_STD_D \
    0x10  ///< IEEE 802.11d Bit Mask. This advertises the country code
#define WIFI_AP_BITMASK_IEEE_STD_H  0x20   ///< IEEE 802.11h Bit Mask. This enables radar detection
#define WIFI_AP_BITMASK_IEEE_STD_N  0x40   ///< IEEE 802.11n (HT) Bit Mask
#define WIFI_AP_BITMASK_IEEE_STD_AC 0x80   ///< IEEE 802.11ac (VHT) Bit Mask
#define WIFI_AP_BITMASK_IEEE_STD_AX 0x100  ///< IEEE 802.11ax (HE) Bit Mask
#define WIFI_AP_BITMASK_IEEE_STD_W  0x200  ///< IEEE 802.11w Bit Mask

// passphrase definitions
#define MIN_PASSPHRASE_LENGTH 8
#define MAX_PASSPHRASE_LENGTH 63

// pre-shared key definitions
#define MAX_PSK_LENGTH 64
#define MAX_PSK_BYTES  65

// length of country code
#define ISO_COUNTRYCODE_LENGTH 2

// Max number of users allowed
#define WIFI_AP_MAX_USERS 1000

// Hardware mode mask
#define HARDWARE_MODE_MASK 0x000F

// max length of an IP address
#define MAX_IP_ADDRESS_LENGTH 15

typedef enum {
    WIFI_AP_SECURITY_NONE = 0,
    ///< WiFi Access Point is open and has no password.

    WIFI_AP_SECURITY_WPA2 = 1
    ///< WiFi Access Point has WPA2 activated.
} wifiAp_SecurityProtocol_t;

// Structure to store WiFi access point data
typedef struct wifiApT_
{
    char *interfaceName;
    char *domainName;
    char *hostName;
    const char *status;

    struct
    {
        uint16_t MIN_CHANNEL_VALUE;
        uint16_t MAX_CHANNEL_VALUE;
    } channel;

    char ssid[33];
    char ip_ap[15];
    char ip_start[15];
    char ip_stop[15];
    char ip_subnet[15];
    char ip_netmask[15];
    char passphrase[MAX_PASSPHRASE_LENGTH + 1];
    char presharedKey[65];
    char countryCode[33];
    char wifiScriptPath[4096];
    bool discoverable;
    int IeeeStdMask;
    uint16_t channelNumber;
    uint32_t maxNumberClient;
    wifiAp_SecurityProtocol_t securityProtocol;

    struct cds_list_head wifiApListHead;
} wifiApT;

// Functions to set the paramaters of wifi access point
int setHostNameParameter(wifiApT *wifiApData, const char *hostName);
int setDomainNameParameter(wifiApT *wifiApData, const char *domainName);
int setSsidParameter(wifiApT *wifiApData, const char *ssid);
int setChannelParameter(wifiApT *wifiApData, int channelNumber);
int setIeeeStandardParameter(wifiApT *wifiApData, int stdMask);
int setPassPhraseParameter(wifiApT *wifiApData, const char *passphrase);
int setPreSharedKeyParameter(wifiApT *wifiApData, const char *preSharedKey);
int setSecurityProtocolParameter(wifiApT *wifiApData, const char *securityProtocol);
int setCountryCodeParameter(wifiApT *wifiApData, const char *countryCode);
int setMaxNumberClients(wifiApT *wifiApData, int maxNumberClients);
int setIpRangeParameters(wifiApT *wifiApData,
                         const char *ip_ap,
                         const char *ip_start,
                         const char *ip_stop,
                         const char *ip_netmask);

#endif
