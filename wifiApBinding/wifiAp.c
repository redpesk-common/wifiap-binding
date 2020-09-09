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

#define _GNU_SOURCE
#define AFB_BINDING_VERSION 3

#include <stdio.h>
#include <string.h>
#include <wrap-json.h>
#include <json-c/json.h>
#include <afb/afb-binding.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include "wifiAp.h"
#include "utilities.h"


// Set of commands to drive the WiFi features.
#define COMMAND_WIFI_HW_START        " WIFI_START"
#define COMMAND_WIFI_HW_STOP         " WIFI_STOP"
#define COMMAND_WIFI_SET_EVENT       " WIFI_SET_EVENT"
#define COMMAND_WIFI_UNSET_EVENT     " WIFI_UNSET_EVENT"
#define COMMAND_WIFIAP_HOSTAPD_START " WIFIAP_HOSTAPD_START"
#define COMMAND_WIFIAP_HOSTAPD_STOP  " WIFIAP_HOSTAPD_STOP"
#define COMMAND_WIFIAP_WLAN_UP       " WIFIAP_WLAN_UP"

// iptables rule to allow/disallow the DHCP port on WLAN interface
#define COMMAND_IPTABLE_DHCP_INSERT  " IPTABLE_DHCP_INSERT"
#define COMMAND_IPTABLE_DHCP_DELETE  " IPTABLE_DHCP_DELETE"
#define COMMAND_DHCP_RESTART         " DHCP_CLIENT_RESTART"

//path to Wifi platform adapter shell script
#define WIFI_SCRIPT_PATH " /tmp/LB_LINK.sh "

#define MAX_SSID_LENGTH 32
#define MIN_SSID_LENGTH 1
#define MAX_SSID_BYTES  33

#define MIN_PASSPHRASE_LENGTH 8
#define MAX_PASSPHRASE_LENGTH 63
#define MAX_PASSPHRASE_BYTES  64

#define MIN_CHANNEL_VALUE_DEF   1 // for 2.4 Ghz wifi
#define MAX_CHANNEL_VALUE_DEF   14
#define MIN_CHANNEL_STD_A   7 // For 6 Ghz wifi
#define MAX_CHANNEL_STD_A   196
#define MIN_CHANNEL_STD_AD  1 // for 60 Ghz wifi
#define MAX_CHANNEL_STD_AD  6

#define  WIFI_AP_BITMASK_IEEE_STD_A 0x1
#define  WIFI_AP_BITMASK_IEEE_STD_B 0x2
#define  WIFI_AP_BITMASK_IEEE_STD_G 0x4
#define  WIFI_AP_BITMASK_IEEE_STD_AD 0x8
#define  WIFI_AP_BITMASK_IEEE_STD_D 0x10
#define  WIFI_AP_BITMASK_IEEE_STD_H 0x20
#define  WIFI_AP_BITMASK_IEEE_STD_N 0x40
#define  WIFI_AP_BITMASK_IEEE_STD_AC 0x80
#define  WIFI_AP_BITMASK_IEEE_STD_AX 0x100
#define  WIFI_AP_BITMASK_IEEE_STD_W 0x200

#define  MAX_PSK_LENGTH   64
#define  MAX_PSK_BYTES    65
#define  TEMP_STRING_MAX_BYTES 1024 // for the HostApDConf temporary File

#define  WIFI_AP_MAX_USERS      10 // Max number of users allowed
#define  ISO_COUNTRYCODE_LENGTH 2 //length of country code

#define  HARDWARE_MODE_MASK 0x000F // Hardware mode mask

//----------------------------------------------------------------------------------------------------------------------

static struct cds_list_head wifiApList;
//The current SSID
static char SavedSsid[MAX_SSID_BYTES];

//Defines whether the SSID is hidden or not
static bool  SavedDiscoverable  = true;

//The current IEEE mask
static int SavedIeeeStdMask = 0x0004;

// Passphrase used for authentication. Used only with WPA/WPA2 protocol.
static char SavedPassphrase[MAX_PASSPHRASE_BYTES] = "";

// The current MAX and MIN channel
static uint16_t MIN_CHANNEL_VALUE = MIN_CHANNEL_VALUE_DEF;
static uint16_t MAX_CHANNEL_VALUE = MAX_CHANNEL_VALUE_DEF;

// The current security protocol
static wifiAp_SecurityProtocol_t SavedSecurityProtocol = WIFI_AP_SECURITY_WPA2;

//Pre-Shared-Key used for authentication. Used only with WPA/WPA2 protocol.
static char      SavedPreSharedKey[MAX_PSK_BYTES]      = "";

// The WiFi channel associated with the SSID ( default 6)
static uint16_t  SavedChannelNumber = 6;

// The maximum number of clients the wifi Access Point can manage
static uint32_t  SavedMaxNumClients = WIFI_AP_MAX_USERS;

// The current country code
static char      SavedCountryCode[33] = { 'F', 'R', '\0'};

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

static int writeApConfigFile(const char * data, FILE *file){

    size_t length;

    if ((NULL == file) || (NULL == data))
    {
        AFB_ERROR("Invalid parameter(s)");
        return -1;
    }

    length = strlen(data);
    if (length > 0)
    {
        if (fwrite(data, 1, length, file) != length)
        {
            AFB_ERROR("Unable to generate the hostapd file.");
            return -1;
        }
    }

    return 0;
}


static int GenerateHostApConfFile(void){

    char tmpConfig[TEMP_STRING_MAX_BYTES];
    FILE *configFile  = NULL;
    int result = -1;

    configFile = fopen(WIFI_HOSTAPD_FILE, "w");
    if (NULL ==  configFile)
    {
        AFB_ERROR("Unable to create hostapd.conf file");
        return -1;
    }
    else AFB_INFO("hostapd.conf file created successfully");

    memset(tmpConfig, '\0', sizeof(tmpConfig));
    // prepare SSID, channel, country code etc in hostapd.conf
    snprintf(tmpConfig, sizeof(tmpConfig), (HOSTAPD_CONFIG_COMMON
            "ssid=%s\nchannel=%d\nmax_num_sta=%d\ncountry_code=%s\nignore_broadcast_ssid=%d\n"),
            (char *)SavedSsid,
            SavedChannelNumber,
            SavedMaxNumClients,
            (char *)SavedCountryCode,
            !SavedDiscoverable);
    // Write common config such as SSID, channel, country code, etc in hostapd.conf
    tmpConfig[TEMP_STRING_MAX_BYTES - 1] = '\0';
    if (writeApConfigFile(tmpConfig, configFile) != 0)
    {
        AFB_ERROR("Unable to set SSID, channel, etc in hostapd.conf");
        goto error;
    }
    else AFB_INFO("AP parameters has been set in hostapd.conf file successfully");

    memset(tmpConfig, '\0', sizeof(tmpConfig));
    // Write security parameters in hostapd.conf
    switch (SavedSecurityProtocol)
    {
        case WIFI_AP_SECURITY_NONE:
            AFB_DEBUG("LE_WIFIAP_SECURITY_NONE");
            result = writeApConfigFile(HOSTAPD_CONFIG_SECURITY_NONE, configFile);
            break;

        case WIFI_AP_SECURITY_WPA2:
            AFB_DEBUG("LE_WIFIAP_SECURITY_WPA2");
            if ('\0' != SavedPassphrase[0])
            {
                snprintf(tmpConfig, sizeof(tmpConfig), (HOSTAPD_CONFIG_SECURITY_WPA2
                        "wpa_passphrase=%s\n"), SavedPassphrase);
                tmpConfig[TEMP_STRING_MAX_BYTES - 1] = '\0';
                result = writeApConfigFile(tmpConfig, configFile);
            }
            else if ('\0' != SavedPreSharedKey[0])
            {
                snprintf(tmpConfig, sizeof(tmpConfig), (HOSTAPD_CONFIG_SECURITY_WPA2
                        "wpa_psk=%s\n"), SavedPreSharedKey);
                tmpConfig[TEMP_STRING_MAX_BYTES - 1] = '\0';
                result = writeApConfigFile(tmpConfig, configFile);
            }
            else
            {
                AFB_ERROR("Security protocol is missing!");
                result = -1;
            }
            break;

        default:
            AFB_ERROR("Unsupported security protocol!");
            result = -1;
            break;
    }

    // Write security parameters in hostapd.conf
    if (result != 0)
    {
        AFB_ERROR("Unable to set security parameters in hostapd.conf");
        goto error;
    }
    else AFB_INFO("Security parameters has been set successfully in hostapd.conf ");

    // prepare IEEE std including hardware mode into hostapd.conf
    memset(tmpConfig, '\0', sizeof(tmpConfig));
    switch( SavedIeeeStdMask & HARDWARE_MODE_MASK )
    {
        case WIFI_AP_BITMASK_IEEE_STD_A:
            utf8_Copy(tmpConfig, "hw_mode=a\n", sizeof(tmpConfig), NULL);
            break;
        case WIFI_AP_BITMASK_IEEE_STD_B:
            utf8_Copy(tmpConfig, "hw_mode=b\n", sizeof(tmpConfig), NULL);
            break;
        case WIFI_AP_BITMASK_IEEE_STD_G:
            utf8_Copy(tmpConfig, "hw_mode=g\n", sizeof(tmpConfig), NULL);
            break;
        case WIFI_AP_BITMASK_IEEE_STD_AD:
            utf8_Copy(tmpConfig, "hw_mode=ad\n", sizeof(tmpConfig), NULL);
            break;
        default:
            utf8_Copy(tmpConfig, "hw_mode=g\n", sizeof(tmpConfig), NULL);
            break;
    }

    if ( SavedIeeeStdMask & WIFI_AP_BITMASK_IEEE_STD_D )
    {
        utf8_Append(tmpConfig, "ieee80211d=1\n", sizeof(tmpConfig), NULL);
    }
    if ( SavedIeeeStdMask & WIFI_AP_BITMASK_IEEE_STD_H )
    {
        utf8_Append(tmpConfig, "ieee80211h=1\n", sizeof(tmpConfig), NULL);
    }
    if ( SavedIeeeStdMask & WIFI_AP_BITMASK_IEEE_STD_N )
    {
        // hw_mode=b does not support ieee80211n, but driver can handle it
        utf8_Append(tmpConfig, "ieee80211n=1\n", sizeof(tmpConfig), NULL);
    }
    if ( SavedIeeeStdMask & WIFI_AP_BITMASK_IEEE_STD_AC )
    {
        utf8_Append(tmpConfig, "ieee80211ac=1\n", sizeof(tmpConfig), NULL);
    }
    if ( SavedIeeeStdMask & WIFI_AP_BITMASK_IEEE_STD_AX )
    {
        utf8_Append(tmpConfig, "ieee80211ax=1\n", sizeof(tmpConfig), NULL);
    }
    if ( SavedIeeeStdMask & WIFI_AP_BITMASK_IEEE_STD_W )
    {
        utf8_Append(tmpConfig, "ieee80211w=1\n", sizeof(tmpConfig), NULL);
    }
    // Write IEEE std in hostapd.conf
    tmpConfig[TEMP_STRING_MAX_BYTES - 1] = '\0';
    if (writeApConfigFile(tmpConfig, configFile) != 0)
    {
        AFB_ERROR("Unable to set IEEE std in hostapd.conf");
        goto error;
    }
    else AFB_INFO("IEEE std has been set successfully in hostapd.conf");
    fclose(configFile);
    return 0;

error:
    fclose(configFile);
    // Remove generated hostapd.conf file
    remove(WIFI_HOSTAPD_FILE);
    return -1;
}


static void start(afb_req_t req)
{

    int     systemResult;
    AFB_INFO("Starting AP ...");

    // Check that an SSID is provided before starting
    if ('\0' == SavedSsid[0])
    {
        AFB_ERROR("Unable to start AP because no valid SSID provided");
        afb_req_fail(req, "failed - Bad parameter", "No valid SSID provided");
        return;
    }

    // Check channel number is properly set before starting
    if ((SavedChannelNumber < MIN_CHANNEL_VALUE) ||
            (SavedChannelNumber > MAX_CHANNEL_VALUE))
    {
        AFB_ERROR("Unable to start AP because no valid channel number provided");
        afb_req_fail(req, "failed - Bad parameter", "No valid channel number provided");
        return;
    }

    // Create hostapd.conf file in /tmp
    if (GenerateHostApConfFile() != 0)
    {
        afb_req_fail(req, "failed", "Failed to generate hostapd.conf");
        return;
    }
    else AFB_INFO("AP configuration file has been generated");

    systemResult = system(WIFI_SCRIPT_PATH COMMAND_WIFI_HW_START);
    /**
     * Returned values:
     *   0: if the interface is correctly moutned
     *  50: if WiFi card is not inserted
     * 100: if WiFi card may not work
     * 127: if driver can not be installed
     *  -1: if the fork() has failed (see man system)
     */

    if (0 == WEXITSTATUS(systemResult))
    {
        AFB_INFO("WiFi hardware started correctly");
    }
    // Return value of 50 means WiFi card is not inserted.
    else if ( WEXITSTATUS(systemResult) == 50)
    {
        AFB_ERROR("WiFi card is not inserted");
        return;
    }
    // Return value of 100 means WiFi card may not work.
    else if ( WEXITSTATUS(systemResult) == 100)
    {
        AFB_ERROR("Unable to reset WiFi card");
        return;
    }
    // WiFi card failed to start.
    else
    {
        AFB_WARNING("Failed to start WiFi AP command \"%s\" systemResult (%d)",
                COMMAND_WIFI_HW_START, systemResult);
        return;
    }

    AFB_INFO("Started WiFi AP command \"%s\" successfully",
                COMMAND_WIFI_HW_START);

    // Start Access Point cmd: /bin/hostapd /etc/hostapd.conf
    systemResult = system(WIFI_SCRIPT_PATH COMMAND_WIFIAP_HOSTAPD_START);
    if ((!WIFEXITED(systemResult)) || (0 != WEXITSTATUS(systemResult)))
    {
        AFB_ERROR("WiFi Client Command \"%s\" Failed: (%d)",
                COMMAND_WIFIAP_HOSTAPD_START,
                systemResult);
        // Remove generated hostapd.conf file
        remove(WIFI_HOSTAPD_FILE);
        goto error;
    }

    AFB_INFO("WiFi AP started correclty");
    afb_req_success(req, NULL, "Access point started successfully");
    return;

error:
    afb_req_fail(req, "failed", "Unspecified internal error\n");
    return;

}

static void stop(afb_req_t req){

    int status;

    afb_api_t wifiAP = afb_req_get_api(req);

    wifiApT *wifiApData = (wifiApT*) afb_api_get_userdata(wifiAP);
    if (!wifiAP)
    {
        afb_req_fail(req,NULL,"wifiAP has no data available!");
        return;
    }

    // Try to delete the rule allowing the DHCP ports on WLAN. Ignore if it fails
    status = system(WIFI_SCRIPT_PATH COMMAND_IPTABLE_DHCP_DELETE);
    if ((!WIFEXITED(status)) || (0 != WEXITSTATUS(status)))
    {
        AFB_WARNING("Deleting rule for DHCP port fails");
    }

    int error = deleteSubnetDeclarationConfig(wifiApData->ip_subnet,wifiApData->ip_subnet,wifiApData->ip_ap,\
                                                wifiApData->ip_start, wifiApData->ip_stop);
    if (error)
    {
        AFB_ERROR("Error while deleting wifiAP subnet configuration");
    }

    status = system(WIFI_SCRIPT_PATH COMMAND_WIFIAP_HOSTAPD_STOP);
    if ((!WIFEXITED(status)) || (0 != WEXITSTATUS(status)))
    {
        AFB_ERROR("WiFi AP Command \"%s\" Failed: (%d)",
                COMMAND_WIFIAP_HOSTAPD_STOP,
                status);
        goto onErrorExit;
    }

    status = system(WIFI_SCRIPT_PATH COMMAND_WIFI_HW_STOP);
    if ((!WIFEXITED(status)) || (0 != WEXITSTATUS(status)))
    {
        AFB_ERROR("WiFi AP Command \"%s\" Failed: (%d)", COMMAND_WIFI_HW_STOP, status);
        goto onErrorExit;
    }

    afb_req_success(req, NULL, "Access Point was stoped successfully");
    return;

onErrorExit:
    afb_req_fail(req, "failed", "Unspecified internal error\n");
    return;

}


static void setSsid(afb_req_t req){

    json_object *ssidJ = afb_req_json(req);
    json_object *responseJ = json_object_new_object();
    const char *ssid = json_object_get_string(ssidJ);
    const uint8_t *ssidPtr;
    size_t ssidNumElements;

    AFB_INFO("Set SSID");

    ssidPtr = (const uint8_t *)ssid;
    ssidNumElements = sizeof(ssid)-1;

    if ((0 < ssidNumElements) && (ssidNumElements <= MAX_SSID_LENGTH))
    {
        // Store SSID to be used later during startup procedure
        memcpy(&SavedSsid[0], (const char *)&ssidPtr[0], ssidNumElements);
        // Make sure there is a null termination
        SavedSsid[ssidNumElements] = '\0';
        AFB_INFO("SSID was set successfully");
        json_object_object_add(responseJ,"SSID", json_object_new_string(SavedSsid));
        afb_req_success(req, responseJ, "SSID set successfully");
    }
    else
    {
        afb_req_fail(req, "failed - Bad parameter", "Wi-Fi - SSID length exceeds MAX_SSID_LENGTH\n");
    }
    return;
}

static void setPassPhrase(afb_req_t req){

    AFB_INFO("Set Passphrase");

    json_object *passphraseJ = afb_req_json(req);
    json_object *responseJ = json_object_new_object();
    const char  *passphrase = json_object_get_string(passphraseJ);

    if (passphrase != NULL)
    {
        size_t length = strlen(passphrase);

        if ((length >= MIN_PASSPHRASE_LENGTH) &&
            (length <= MAX_PASSPHRASE_LENGTH))
        {
            // Store Passphrase to be used later during startup procedure
            strncpy(&SavedPassphrase[0], &passphrase[0], length);
            // Make sure there is a null termination
            SavedPassphrase[length] = '\0';
            AFB_INFO("Passphrase was set successfully");
            json_object_object_add(responseJ,"Passphrase", json_object_new_string(SavedPassphrase));
            afb_req_success(req, responseJ, "Passphrase set successfully!");
            return;
        }
        afb_req_fail(req, "failed - Bad parameter", "Wi-Fi - PassPhrase with Invalid length ");
    }
    afb_req_fail(req, "invalid-syntax", "Missing passPhrase");
    return;

}

static void setDiscoverable(afb_req_t req){

    AFB_INFO("Set Discoverable");

    json_object *isDiscoverableJ = afb_req_json(req);
    json_object *responseJ = json_object_new_object();
    bool isDiscoverable = json_object_get_boolean(isDiscoverableJ);

    SavedDiscoverable = isDiscoverable;

    AFB_INFO("AP is set as discoverable");
    json_object_object_add(responseJ,"isDiscoverable", json_object_new_boolean(SavedDiscoverable));
    afb_req_success(req, responseJ, "AP discoverability was set successfully");

    return;
}

static void setIeeeStandard(afb_req_t req){

    afb_api_t wifiAP = afb_req_get_api(req);
    json_object *IeeeStandardJ = afb_req_json(req);
    json_object *responseJ = json_object_new_object();
    int stdMask = json_object_get_int(IeeeStandardJ);


    wifiApT *wifiApData = (wifiApT*) afb_api_get_userdata(wifiAP);
    if (!wifiAP)
    {
        afb_req_fail(req,NULL,"wifiAP has no data available!");
        return;
    }

    int8_t hwMode = stdMask & 0x0F;
    int8_t numCheck = (hwMode & 0x1) + ((hwMode >> 1) & 0x1) +
                       ((hwMode >> 2) & 0x1) + ((hwMode >> 3) & 0x1);

    AFB_INFO("Set IeeeStdBitMask : 0x%X", stdMask);
    //Hardware mode should be set.
    if (numCheck == 0)
    {
        AFB_WARNING("No hardware mode is set");
        goto onErrorExit;
    }
    //Hardware mode should be exclusive.
    if ( numCheck > 1 )
    {
        AFB_WARNING("Only one hardware mode can be set");
        goto onErrorExit;
    }

    if ( stdMask & WIFI_AP_BITMASK_IEEE_STD_AC )
    {
        // ieee80211ac=1 only works with hw_mode=a
        if ((stdMask & WIFI_AP_BITMASK_IEEE_STD_A) == 0)
        {
            AFB_WARNING("ieee80211ac=1 only works with hw_mode=a");
            goto onErrorExit;
        }
    }

    if ( stdMask & WIFI_AP_BITMASK_IEEE_STD_H )
    {
        // ieee80211h=1 can be used only with ieee80211d=1
        if ((stdMask & WIFI_AP_BITMASK_IEEE_STD_D) == 0)
        {
            AFB_WARNING("ieee80211h=1 only works with ieee80211d=1");
            goto onErrorExit;
        }
    }

    wifiApData->IeeeStdMask = stdMask;
    AFB_INFO("IeeeStdBitMask was set successfully");

    json_object_object_add(responseJ,"stdMask", json_object_new_int(wifiApData->IeeeStdMask));
    afb_req_success(req, responseJ, "stdMask is set successfully");
    return;
onErrorExit:
    afb_req_fail(req, "Failed", "Parameter is invalid!");
    return;
}

static void getIeeeStandard(afb_req_t req){

    AFB_INFO("Getting IEEE standard ...");

    afb_api_t wifiAP = afb_req_get_api(req);

    wifiApT *wifiApData = (wifiApT*) afb_api_get_userdata(wifiAP);
    if (!wifiAP)
    {
        afb_req_fail(req,NULL,"wifiAP has no data available!");
        return;
    }

    json_object *responseJ = json_object_new_object();
    AFB_API_INFO(wifiAP,"DONE");
    json_object_object_add(responseJ,"stdMask", json_object_new_int(wifiApData->IeeeStdMask));
    afb_req_success_f(req, responseJ, NULL);

    return;
}

static void setChannel(afb_req_t req){

    AFB_INFO("Set channel number");

    json_object *channelNumberJ = afb_req_json(req);
    json_object *responseJ = json_object_new_object();
    int channelNumber = json_object_get_int(channelNumberJ);

    int8_t hwMode = SavedIeeeStdMask & 0x0F;

    switch (hwMode)
    {
        case WIFI_AP_BITMASK_IEEE_STD_A:
            MIN_CHANNEL_VALUE = MIN_CHANNEL_STD_A;
            MAX_CHANNEL_VALUE = MAX_CHANNEL_STD_A;
            break;
        case WIFI_AP_BITMASK_IEEE_STD_B:
        case WIFI_AP_BITMASK_IEEE_STD_G:
            MIN_CHANNEL_VALUE = MIN_CHANNEL_VALUE_DEF;
            MAX_CHANNEL_VALUE = MAX_CHANNEL_VALUE_DEF;
            break;
        case WIFI_AP_BITMASK_IEEE_STD_AD:
            MIN_CHANNEL_VALUE = MIN_CHANNEL_STD_AD;
            MAX_CHANNEL_VALUE = MAX_CHANNEL_STD_AD;
            break;
        default:
            AFB_WARNING("Invalid hardware mode");
    }

    if ((channelNumber >= MIN_CHANNEL_VALUE) &&
        (channelNumber <= MAX_CHANNEL_VALUE))
    {
        SavedChannelNumber = channelNumber;
        AFB_INFO("Channel number was set successfully");
        json_object_object_add(responseJ,"channelNumber", json_object_new_int(SavedChannelNumber));
        afb_req_success_f(req, responseJ, NULL);
       return;
    }
    else
    {
        afb_req_fail(req, "Failed", "Invalid parameter");
        return;
    }
}

static void setSecurityProtocol(afb_req_t req){

    AFB_INFO("Set security protocol");
    json_object *securityProtocolJ = afb_req_json(req);
    const char * securityProtocol = json_object_get_string(securityProtocolJ);

    if (!securityProtocol) {
        afb_req_fail_f(req, "syntaxe", "NULL");
    }
    if (!strcasecmp(securityProtocol,"none")) {
        SavedSecurityProtocol = WIFI_AP_SECURITY_NONE;
        afb_req_success(req,NULL,"Security parameter was set to none!");
    }
    else if (!strcasecmp(securityProtocol,"WPA2")){
        SavedSecurityProtocol = WIFI_AP_SECURITY_WPA2;
        afb_req_success(req,NULL,"Security parameter was set to WPA2!");
    }
    else afb_req_fail(req, NULL, "Parameter is invalid!");

    AFB_INFO("Security protocol was set successfully");
    return;
}

static void SetPreSharedKey(afb_req_t req){

    AFB_INFO("Set preSharedKey");
    json_object *preSharedKeyJ = afb_req_json(req);
    const char * preSharedKey = json_object_get_string(preSharedKeyJ);
    json_object *responseJ = json_object_new_object();

    if (preSharedKey != NULL)
    {
        uint32_t length = strlen(preSharedKey);

        if (length <= MAX_PSK_LENGTH)
        {
            // Store PSK to be used later during startup procedure
            utf8_Copy(SavedPreSharedKey, preSharedKey, sizeof(SavedPreSharedKey), NULL);

            AFB_INFO("PreSharedKey was set successfully to %s",SavedPreSharedKey);
            json_object_object_add(responseJ,"preSharedKey", json_object_new_string(SavedPreSharedKey));
            afb_req_success(req,responseJ,"PreSharedKey was set successfully!");
        }
        else afb_req_fail(req, NULL, "Parameter is invalid!");

    }
    return;
}

static void setCountryCode(afb_req_t req){

    AFB_INFO("Set country code");
    json_object *countryCodeJ = afb_req_json(req);
    const char * countryCode = json_object_get_string(countryCodeJ);
    json_object *responseJ = json_object_new_object();

    if (countryCode != NULL)
    {
        uint32_t length = strlen(countryCode);

        if (length == ISO_COUNTRYCODE_LENGTH)
        {
            strncpy(&SavedCountryCode[0], &countryCode[0], length );
            SavedCountryCode[length] = '\0';

            AFB_INFO("country code was set to %s",SavedCountryCode);
            json_object_object_add(responseJ,"countryCode", json_object_new_string(SavedCountryCode));
            afb_req_success(req,responseJ,"country code was set successfully");
            return;
        }
    }
    else afb_req_fail(req, NULL, "Parameter is invalid");
    return;

}

static void SetMaxNumberClients(afb_req_t req){

    AFB_INFO("Set the maximum number of clients");

    json_object *maxNumberClientsJ = afb_req_json(req);
    json_object *responseJ = json_object_new_object();
    int maxNumberClients = json_object_get_int(maxNumberClientsJ);

    if ((maxNumberClients >= 1) && (maxNumberClients <= WIFI_AP_MAX_USERS))
    {
       SavedMaxNumClients = maxNumberClients;

       AFB_NOTICE("The maximum number of clients was set to %d",SavedMaxNumClients);
       json_object_object_add(responseJ,"maxNumberClients", json_object_new_int(SavedMaxNumClients));
       afb_req_success(req,responseJ,"Max Number of clients was set successfully!");
    }
    else afb_req_fail(req, NULL, "The value is out of range");
    return;

}


/*******************************************************************************
 *     Set the access point IP address and client IP  addresses rang           *
 ******************************************************************************/
static void setIpRange (afb_req_t req)
{
    afb_api_t wifiAP = afb_req_get_api(req);
    json_object *argsJ = afb_req_json(req);
    const char *ip_ap, *ip_start, *ip_stop, *ip_netmask;

    wifiApT *wifiApData = (wifiApT*) afb_api_get_userdata(wifiAP);
    if (!wifiAP)
    {
        afb_req_fail(req,NULL,"wifiAP has no data available!");
        return;
    }

    /*
        ip_ap    : Access point's IP address
        ip_start : Access Point's IP address start
        ip_stop  : Access Point's IP address stop
    */

    int error = wrap_json_unpack(argsJ, "{ss,ss,ss,ss !}"
            , "ip_ap"          , &ip_ap
            , "ip_start"       , &ip_start
            , "ip_stop"        , &ip_stop
            , "ip_netmask"     , &ip_netmask
        );
    if (error) {
        afb_req_fail_f(req,
                     "invalid-syntax",
					 "%s  missing 'ip_ap|ip_start|ip_stop|ip_netmask' error=%s args=%s",
					 __func__, wrap_json_get_error_string(error), json_object_get_string(argsJ));
		return;
	}

    struct sockaddr_in  saApPtr;
    struct sockaddr_in  saStartPtr;
    struct sockaddr_in  saStopPtr;
    struct sockaddr_in  saNetmaskPtr;
    struct sockaddr_in  saSubnetPtr;
    const char         *parameterPtr = 0;

    // Check the parameters
    if ((ip_ap == NULL) || (ip_start == NULL) || (ip_stop == NULL) || (ip_netmask == NULL))
    {
        goto OnErrorExit;
    }

    if ((!strlen(ip_ap)) || (!strlen(ip_start)) || (!strlen(ip_stop)) || (!strlen(ip_netmask)))
    {
        goto OnErrorExit;
    }

    if (inet_pton(AF_INET, ip_ap, &(saApPtr.sin_addr)) <= 0)
    {
        parameterPtr = "AP";
    }
    else if (inet_pton(AF_INET, ip_start, &(saStartPtr.sin_addr)) <= 0)
    {
        parameterPtr = "start";
    }
    else if (inet_pton(AF_INET, ip_stop, &(saStopPtr.sin_addr)) <= 0)
    {
        parameterPtr = "stop";
    }
    else if (inet_pton(AF_INET, ip_netmask, &(saNetmaskPtr.sin_addr)) <= 0)
    {
        parameterPtr = "Netmask";
    }

    // get the subnet@  from AP IP@
    saSubnetPtr.sin_addr.s_addr = saApPtr.sin_addr.s_addr & saNetmaskPtr.sin_addr.s_addr;
    const char *ip_subnet = inet_ntoa(saSubnetPtr.sin_addr);

    if (parameterPtr != NULL)
    {
        AFB_ERROR("Invalid %s IP address", parameterPtr);
        return;
    }
    else
    {
        unsigned int ap = ntohl(saApPtr.sin_addr.s_addr);
        unsigned int start = ntohl(saStartPtr.sin_addr.s_addr);
        unsigned int stop = ntohl(saStopPtr.sin_addr.s_addr);
        unsigned int netmask = ntohl(saNetmaskPtr.sin_addr.s_addr);
        unsigned int subnet = ntohl(saSubnetPtr.sin_addr.s_addr);

        AFB_INFO("@AP=%x, @APstart=%x, @APstop=%x, @APnetmask=%x @APnetid=%x",
                ap, start, stop, netmask, subnet);

        if (start > stop)
        {
            AFB_INFO("Need to swap start & stop IP addresses");
            start = start ^ stop;
            stop = stop ^ start;
            start = start ^ stop;
        }

        if ((ap >= start) && (ap <= stop))
        {
            AFB_ERROR("AP IP address is within the range");
            goto OnErrorExit;
        }
    }

    {
        char cmd[256];
        int  systemResult;

        snprintf((char *)&cmd, sizeof(cmd), " %s %s %s",
                WIFI_SCRIPT_PATH,
                COMMAND_WIFIAP_WLAN_UP,
                ip_ap);

        systemResult = system(cmd);
        if ( WEXITSTATUS (systemResult) != 0)
        {
            AFB_ERROR("Unable to mount the network interface");
            goto OnErrorExit;
        }
        else
        {
            FILE *filePtr;

            filePtr = fopen (DHCP_CFG_LINK, "a");

            if (!checkFileExists(DHCP_CFG_LINK))
            {
                AFB_INFO("Creation of dhcp configuration file (%s)", DHCP_CFG_FILE);

                if (symlink(DHCP_CFG_FILE, DHCP_CFG_LINK))
                {
                    AFB_ERROR("Unable to create link to dhcp configuration file: %m.");
                    goto OnErrorExit;
                }
                FILE *filePtrTmp = fopen (DHCP_CFG_FILE, "w");
                /*
                * Write the following to the dhcp config file :
                        option domain-name "iotbzh.lan";
                        option domain-name-servers ns1.iotbzh.lan, ns2.iotbzh.lan;
                        default-lease-time 3600;
                        max-lease-time 7200;
                        authoritative;

                        subnet 192.168.5.0 netmask 255.255.255.0 {
                                option routers                  192.168.5.1;
                                option subnet-mask              255.255.255.0;
                                option domain-search            "iotbzh.lan";
                                range   192.168.5.10   192.168.5.100;
                        }
                    *
                */

                if (filePtrTmp != NULL)
                {
                    //Interface is generated when COMMAND_DHCP_RESTART called
                    fprintf(filePtr, DHCP_CONFIG_COMMON);
                    fprintf(filePtr, "subnet %s netmask %s {\n",ip_subnet ,ip_netmask);
                    fprintf(filePtr, "option routers %s;\n",ip_ap);
                    fprintf(filePtr, "option subnet-mask %s;\n",ip_netmask);
                    fprintf(filePtr, "option domain-search    \"iotbzh.lan\";\n");
                    fprintf(filePtr, "range %s %s;}\n", ip_start, ip_stop);
                    fclose(filePtr);
                }
                else
                {
                    AFB_ERROR("Unable to open the dhcp configuration file: %m.");
                    goto OnErrorExit;
                }
            }
            else
            {
                AFB_INFO("Creation of dhcp configuration file (%s)", DHCP_CFG_LINK);
                /*
                * Write the following to the dhcp config file :
                *
                    subnet 192.168.5.0 netmask 255.255.255.0 {
                            option routers                  192.168.5.1;
                            option subnet-mask              255.255.255.0;
                            option domain-search            "iotbzh.lan";
                            range   192.168.5.10   192.168.5.100;
                    }
                *
                */

                if (filePtr != NULL)
                {
                    //Interface is generated when COMMAND_DHCP_RESTART called
                    fprintf(filePtr, "subnet %s netmask %s {\n",ip_subnet ,ip_netmask);
                    fprintf(filePtr, "option routers %s;\n",ip_ap);
                    fprintf(filePtr, "option subnet-mask %s;\n",ip_netmask);
                    fprintf(filePtr, "option domain-search    \"iotbzh.lan\";\n");
                    fprintf(filePtr, "range %s %s;}\n", ip_start, ip_stop);
                    fclose(filePtr);
                }
                else
                {
                    AFB_ERROR("Unable to open the dhcp configuration file: %m.");
                    goto OnErrorExit;
                }
            }

            AFB_INFO("@AP=%s, @APstart=%s, @APstop=%s", ip_ap, ip_start, ip_stop);

            // Insert the rule allowing the DHCP ports on WLAN
            systemResult = system(WIFI_SCRIPT_PATH COMMAND_IPTABLE_DHCP_INSERT);
            if (WEXITSTATUS (systemResult) != 0)
            {
                AFB_ERROR("Unable to allow DHCP ports.");
                goto OnErrorExit;
            }

            systemResult = system(WIFI_SCRIPT_PATH COMMAND_DHCP_RESTART);
            if (WEXITSTATUS (systemResult) != 0)
            {
                AFB_ERROR("Unable to restart the DHCP server.");
                goto OnErrorExit;
            }
        }
    }
    afb_req_success(req,NULL,"IP range was set successfully!");
    return;
OnErrorExit:
    afb_req_fail_f(req, "Failed", NULL);
    return;
}

/*******************************************************************************
 *               Initialize the wifi data structure                            *
 ******************************************************************************/
static int initWifiApData(afb_api_t api, wifiApT *wifiApData){

    wifiApData->discoverable     = true;
    wifiApData->IeeeStdMask      = 0x0004;
    wifiApData->channelNumber    = 6;
    wifiApData->securityProtocol = WIFI_AP_SECURITY_WPA2;
    wifiApData->maxNumberClient  = WIFI_AP_MAX_USERS;


    strcpy(wifiApData->passphrase, "");
    strcpy(wifiApData->presharedKey, "");
    strcpy(wifiApData->countryCode, "FR");

    return 0;
}

/*******************************************************************************
 *		WiFi Access Point verbs table					       *
 ******************************************************************************/

static const afb_verb_t verbs[] = {
    { .verb = "start"               , .callback = start ,              .info = "start the wifi access point service"},
    { .verb = "stop"                , .callback = stop ,               .info = "stop the wifi access point service"},
    { .verb = "setSsid"             , .callback = setSsid ,            .info = "set the wifiAp SSID"},
    { .verb = "setPassPhrase"       , .callback = setPassPhrase ,      .info = "set the wifiAp passphrase"},
    { .verb = "setDiscoverable"     , .callback = setDiscoverable ,    .info = "set if access point announce its presence"},
    { .verb = "setIeeeStandard"     , .callback = setIeeeStandard ,    .info = "set which IEEE standard to use "},
    { .verb = "setChannel"          , .callback = setChannel ,         .info = "set which wifi channel to use"},
    { .verb = "getIeeeStandard"     , .callback = getIeeeStandard ,    .info = "get which IEEE standard is used"},
    { .verb = "setSecurityProtocol" , .callback = setSecurityProtocol ,.info = "set which security protocol to use"},
    { .verb = "setPreSharedKey"     , .callback = SetPreSharedKey ,    .info = "set the pre-shared key"},
    { .verb = "setIpRange"          , .callback = setIpRange ,         .info = "define the access point IP address and client IP  addresses range"},
    { .verb = "setCountryCode"      , .callback = setCountryCode ,     .info = "set the country code to use for regulatory domain"},
    { .verb = "SetMaxNumberClients" , .callback = SetMaxNumberClients, .info = "Set the maximum number of clients allowed to be connected to WiFiAP at the same time"},
    { .verb = "subscribe"           , .callback = NULL ,               .info = "subscribe to wifiAp events unimplemented"},
    { .verb = "unsubscribe"         , .callback = NULL ,               .info = "unsubscribe to wifiAp events unimplemented"}
};


/*******************************************************************************
 *                      Initialize the binding                                 *
 ******************************************************************************/
static int init_wifi_AP_binding(afb_api_t api)
{
    if (!api)
        return -1;

    AFB_API_NOTICE(api, "Binding start ...");

    wifiApT *wifiApData = (wifiApT *) calloc(1, sizeof(wifiApT));

    CDS_INIT_LIST_HEAD(&wifiApList);
    CDS_INIT_LIST_HEAD(&wifiApData->wifiApListHead);
    initWifiApData(api, wifiApData);
	if(! wifiApData)
		return -2;

    afb_api_set_userdata(api, wifiApData);
    cds_list_add_tail(&wifiApData->wifiApListHead, &wifiApList);

	return 0;
}


const afb_binding_t afbBindingExport = {
    .api = "wifiAp",
	.specification = NULL,
	.verbs = verbs,
	.preinit = NULL,
	.init = init_wifi_AP_binding,
	.onevent = NULL,
	.userdata = NULL,
	.provide_class = NULL,
	.require_class = NULL,
	.require_api = NULL,
	.noconcurrency = 0
};