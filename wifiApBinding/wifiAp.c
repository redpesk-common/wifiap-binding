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
#include "wifiAp.h"
#include "utilities.h"

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

#define  WIFI_AP_MAX_USERS  10 // Max number of users allowed

#define  HARDWARE_MODE_MASK 0x000F // Hardware mode mask

//----------------------------------------------------------------------------------------------------------------------

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
static char SavedPreSharedKey[MAX_PSK_BYTES]      = "";

// The WiFi channel associated with the SSID ( default 6)
static uint16_t  SavedChannelNumber = 6;

// The maximum number of clients the wifi Access Point can manage
static uint32_t  SavedMaxNumClients = WIFI_AP_MAX_USERS;

// The current country code
static char      SavedCountryCode[33] = { 'U', 'S', '\0'}; // TODO : check if '33' is the right value

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

//----------------------------------------------------------------------------------------------------------------------
static struct event *events = NULL;

static int writeApConfigFile(const char * data, FILE *file){

    size_t length;

    if ((NULL == file) || (NULL == data))
    {
        AFB_API_ERROR(afbBindingV3root,"Invalid parameter(s)");
        return -1;
    }

    length = strlen(data);
    if (length > 0)
    {
        if (fwrite(data, 1, length, file) != length)
        {
            AFB_API_ERROR(afbBindingV3root,"Unable to generate the hostapd file.");
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
        AFB_API_ERROR(afbBindingV3root,"Unable to create hostapd.conf file.");
        return -1;
    }

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
        AFB_API_ERROR(afbBindingV3root,"Unable to set SSID, channel, etc in hostapd.conf");
        goto error;
    }

    memset(tmpConfig, '\0', sizeof(tmpConfig));
    // Write security parameters in hostapd.conf
    switch (SavedSecurityProtocol)
    {
        case WIFI_AP_SECURITY_NONE:
            AFB_API_DEBUG(afbBindingV3root,"LE_WIFIAP_SECURITY_NONE");
            result = writeApConfigFile(HOSTAPD_CONFIG_SECURITY_NONE, configFile);
            break;

        case WIFI_AP_SECURITY_WPA2:
            AFB_API_DEBUG(afbBindingV3root,"LE_WIFIAP_SECURITY_WPA2");
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
                AFB_API_ERROR(afbBindingV3root,"Security protocol is missing!");
                result = -1;
            }
            break;

        default:
            AFB_API_ERROR(afbBindingV3root,"Unsupported security protocol!");
            result = -1;
            break;
    }

    // Write security parameters in hostapd.conf
    if (result != 0)
    {
        AFB_API_ERROR(afbBindingV3root,"Unable to set security parameters in hostapd.conf");
        goto error;
    }

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
        AFB_API_ERROR(afbBindingV3root,"Unable to set IEEE std in hostapd.conf");
        goto error;
    }
    fclose(configFile);
    return 0;

error:
    fclose(configFile);
    // Remove generated hostapd.conf file
    remove(WIFI_HOSTAPD_FILE);
    return -1;
}


static void start(afb_req_t req){

    int     systemResult;

    // Check that an SSID is provided before starting
    if ('\0' == SavedSsid[0])
    {
        AFB_API_ERROR(afbBindingV3root,"Unable to start AP because no valid SSID provided");
        afb_req_fail(req, "failed - Bad parameter", "No valid SSID provided");
        return;
    }

    // Check channel number is properly set before starting
    if ((SavedChannelNumber < MIN_CHANNEL_VALUE) ||
            (SavedChannelNumber > MAX_CHANNEL_VALUE))
    {
        AFB_API_ERROR(afbBindingV3root,"Unable to start AP because no valid channel number provided");
        afb_req_fail(req, "failed - Bad parameter", "No valid channel number provided");
        return;
    }

    //TODO : add function o generate host AP config file

}

static void stop(afb_req_t req){
 // TODO  : implement this function
}


static void setSsid(afb_req_t req){

    json_object *argsJ = afb_req_json(req);
    const char *ssid;
    const uint8_t *ssidPtr;
    size_t ssidNumElements;
    int error = wrap_json_unpack(argsJ, "{ss,sd !}"
            , "ssid"         , &ssid
            , "length"       , &ssidNumElements
        );
    if (error) {
        afb_req_fail_f(req,
                     "invalid-syntax",
					 "%s  missing 'ssid|length' error=%s args=%s",
					 __func__, wrap_json_get_error_string(error), json_object_get_string(argsJ));
		return;
	}

    ssidPtr = (uint8_t) atoi(ssid);

    if ((0 < ssidNumElements) && (ssidNumElements <= MAX_SSID_LENGTH))
    {
        // Store SSID to be used later during startup procedure
        memcpy(&SavedSsid[0], (const char *)&ssidPtr[0], ssidNumElements);
        // Make sure there is a null termination
        SavedSsid[ssidNumElements] = '\0';
        afb_req_success(req, NULL, NULL);
    }
    else
    {
        afb_req_fail(req, "failed - Bad parameter", "Wi-Fi - SSID length exceeds MAX_SSID_LENGTH\n");
    }
    return;
}

static void setPassPhrase(afb_req_t req){

    const char *passphrase = afb_req_value(req, "passPhrase");

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
            afb_req_success(req, NULL, NULL);
        }
    }
    return;

}

static void setDiscoverable(afb_req_t req){

    const char *isDiscoverable = afb_req_value(req, "passPhrase");
    SavedDiscoverable = isDiscoverable;

    afb_req_success(req, NULL, NULL);
}

static void setIeeeStandard(afb_req_t req){

    const char *argReq = afb_req_value(req, "stdMask");
    int stdMask = atoi(argReq);

    int8_t hwMode = stdMask & 0x0F;
    int8_t numCheck = (hwMode & 0x1) + ((hwMode >> 1) & 0x1) +
                       ((hwMode >> 2) & 0x1) + ((hwMode >> 3) & 0x1);
    AFB_INFO("Set IeeeStdBitMask : 0x%X", stdMask);
    //Hardware mode should be set.
    if ( 0 == numCheck )
    {
        AFB_API_WARNING(afbBindingV3root,"No hardware mode is set.");
        afb_req_fail(req, NULL, "Parameter is invalid");
    }
    //Hardware mode should be exclusive.
    if ( numCheck > 1 )
    {
        AFB_API_WARNING(afbBindingV3root,"Only one hardware mode can be set.");
        afb_req_fail(req, NULL, "Parameter is invalid");
    }

    if ( stdMask & WIFI_AP_BITMASK_IEEE_STD_AC )
    {
        // ieee80211ac=1 only works with hw_mode=a
        if ((stdMask & WIFI_AP_BITMASK_IEEE_STD_A) == 0)
        {
            AFB_API_WARNING(afbBindingV3root,"ieee80211ac=1 only works with hw_mode=a.");
            afb_req_fail(req, NULL, "Parameter is invalid");
        }
    }

    if ( stdMask & WIFI_AP_BITMASK_IEEE_STD_H )
    {
        // ieee80211h=1 can be used only with ieee80211d=1
        if ((stdMask & WIFI_AP_BITMASK_IEEE_STD_D) == 0)
        {
            AFB_API_WARNING(afbBindingV3root,"ieee80211h=1 only works with ieee80211d=1.");
            afb_req_fail(req, NULL, "Parameter is invalid");
        }
    }

    SavedIeeeStdMask = stdMask;
    afb_req_success(req, NULL, NULL);
    return;
}

static void getIeeeStandard(afb_req_t req){
    afb_req_success_f(req, json_object_new_int(SavedIeeeStdMask), NULL);
    return;
}

static void setChannel(afb_req_t req){

    const char * argReq = afb_req_value(req, "channelNumber");
    int channelNumber = atoi(argReq);

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
            AFB_API_WARNING(afbBindingV3root,"Invalid hardware mode");
    }

    if ((channelNumber >= MIN_CHANNEL_VALUE) &&
        (channelNumber <= MAX_CHANNEL_VALUE))
    {
       SavedChannelNumber = channelNumber;
       afb_req_success(req,NULL,NULL);
    }
    return;
}

static void setSecurityProtocol(afb_req_t req){

    const char * securityProtocol = afb_req_value(req, "securityProtocol");

    if (!strcasecmp(securityProtocol,"none")) ;
    else if (!strcasecmp(securityProtocol,"WPA2")){
        SavedSecurityProtocol = WIFI_AP_SECURITY_WPA2;
        afb_req_success(req,NULL,NULL);
    }
    else afb_req_fail(req, NULL, "Parameter is invalid");

    return;
}

static void SetPreSharedKey(afb_req_t req){
    const char *preSharedKey = afb_req_value(req,"preSharedKey");

    if (NULL != preSharedKey)
    {
        uint32_t length = strlen(preSharedKey);

        if (length <= MAX_PSK_LENGTH)
        {
            // Store PSK to be used later during startup procedure
            utf8_Copy(SavedPreSharedKey, preSharedKey, sizeof(SavedPreSharedKey), NULL);
            afb_req_success(req,NULL,NULL);
        }
        else afb_req_fail(req, NULL, "Parameter is invalid");

    }
    return;
}


static const afb_verb_t verbs[] = {
    { .verb = "start"               , .callback = start ,              .session = AFB_SESSION_NONE},
    { .verb = "stop"                , .callback = stop ,               .session = AFB_SESSION_NONE},
    { .verb = "setSsid"             , .callback = setSsid ,            .session = AFB_SESSION_NONE},
    { .verb = "setPassPhrase"       , .callback = setPassPhrase ,      .session = AFB_SESSION_NONE},
    { .verb = "setDiscoverable"     , .callback = setDiscoverable ,    .session = AFB_SESSION_NONE},
    { .verb = "setIeeeStandard"     , .callback = setIeeeStandard ,    .session = AFB_SESSION_NONE},
    { .verb = "setChannel"          , .callback = setChannel ,         .session = AFB_SESSION_NONE},
    { .verb = "getIeeeStandard"     , .callback = getIeeeStandard ,    .session = AFB_SESSION_NONE},
    { .verb = "setSecurityProtocol" , .callback = setSecurityProtocol ,.session = AFB_SESSION_NONE},
    { .verb = "SetPreSharedKey"     , .callback = SetPreSharedKey ,    .session = AFB_SESSION_NONE},
    { .verb = "subscribe"           , .callback = NULL ,               .session = AFB_SESSION_NONE},
    { .verb = "unsubscribe"         , .callback = NULL ,               .session = AFB_SESSION_NONE}
};

const afb_binding_t afbBindingExport = {
    .api = "wifiAp",
	.specification = NULL,
	.verbs = verbs,
	.preinit = NULL,
	.init = NULL,
	.onevent = NULL,
	.userdata = NULL,
	.provide_class = NULL,
	.require_class = NULL,
	.require_api = NULL,
	.noconcurrency = 0
};