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
#ifndef CONFIG_HEADER_FILE
#define CONFIG_HEADER_FILE

#define AFB_BINDING_VERSION 3

#include <afb/afb-binding.h>
#include <stdio.h>
#include "wifi-ap-data.h"

// for the HostApDConf temporary File
#define  TEMP_STRING_MAX_BYTES 1024


// WiFi access point configuration file
#define WIFI_HOSTAPD_FILE "/tmp/hostapd.conf"

//----------------------------------------------------------------------------------------------------------------------
// Host access point global configuration
#define HOSTAPD_CONFIG_COMMON \
    "driver=nl80211\n"\
    "wmm_enabled=1\n"\
    "beacon_int=100\n"\
    "dtim_period=2\n"\
    "rts_threshold=2347\n"\
    "fragm_threshold=2346\n"\
    "ctrl_interface=/var/run/hostapd\n"\
    "ctrl_interface_group=0\n"

// Host access point configuration with no security
#define HOSTAPD_CONFIG_SECURITY_NONE \
    "auth_algs=1\n"\
    "eap_server=0\n"\
    "eapol_key_index_workaround=0\n"\
    "macaddr_acl=0\n"

// Host access point configuration with WPA2 security
#define HOSTAPD_CONFIG_SECURITY_WPA2 \
    "wpa=2\n"\
    "wpa_key_mgmt=WPA-PSK\n"\
    "wpa_pairwise=CCMP\n"\
    "rsn_pairwise=CCMP\n"

// DHCP general configuration
#define DHCP_CONFIG_COMMON \
    "option domain-name \"iotbzh.lan\";\n"\
    "option domain-name-servers ns1.iotbzh.lan, ns2.iotbzh.lan;\n"\
    "default-lease-time 3600;\n"\
    "max-lease-time 7200;\n"\
    "authoritative;\n"

//----------------------------------------------------------------------------------------------------------------------


int createDnsmasqConfigFile(const char *ip_ap, const char *ip_start, const char *ip_stop);
int GenerateHostApConfFile(wifiApT *wifiApData);
int writeApConfigFile(const char * data, FILE *file);
#endif