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
#include <ctl-config.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>

#include "filescan-utils.h"
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
#define WIFI_SCRIPT "var/LB_LINK.sh"
#define PATH_MAX 8192

#define MAX_SSID_LENGTH 32
#define MIN_SSID_LENGTH 1
#define MAX_SSID_BYTES  33

#define MAX_IP_ADDRESS_LENGTH 15

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
// The current MAX and MIN channel
static uint16_t MIN_CHANNEL_VALUE = MIN_CHANNEL_VALUE_DEF;
static uint16_t MAX_CHANNEL_VALUE = MAX_CHANNEL_VALUE_DEF;

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


char *getBindingParentDirPath(afb_api_t apiHandle)
{
    int ret;
    char *bindingDirPath, *bindingParentDirPath = NULL;

    if(! apiHandle)
        return NULL;

    bindingDirPath = GetRunningBindingDirPath(apiHandle);
    if(! bindingDirPath)
        return NULL;

    ret = asprintf(&bindingParentDirPath, "%s/..", bindingDirPath);
    free(bindingDirPath);
    if(ret <= 3)
        return NULL;

    return bindingParentDirPath;
}

/*
    Get The path to the wifi access point handling script
 */

int getScriptPath(afb_api_t apiHandle, char *buffer, size_t size)
{

    AFB_INFO("Get wifi access point script path");

    size_t searchPathLength;
    char *searchPath, *binderRootDirPath, *bindingParentDirPath;

    if(! apiHandle)
        return 0;

    binderRootDirPath = GetAFBRootDirPath(apiHandle);
    if(! binderRootDirPath)
        return 0;

    AFB_INFO("GetAFBRootDirPath = %s",binderRootDirPath);

    bindingParentDirPath = getBindingParentDirPath(apiHandle);
    if(! bindingParentDirPath) {
        free(binderRootDirPath);
        return 0;
    }
    AFB_INFO("GetBindingParentDirPath = %s",bindingParentDirPath);

    /* Allocating with the size of binding root dir path + binding parent directory path
     * + 1 character for the NULL terminating character + 1 character for the additional separator
     * between binderRootDirPath and bindingParentDirPath + 2*4 char for '/etc suffixes'.
     */
    searchPathLength = strlen(binderRootDirPath) + strlen(bindingParentDirPath) + 2*strlen(WIFI_SCRIPT) + 10;

    searchPath = malloc(searchPathLength);
    if(! searchPath) {
        free(binderRootDirPath);
        free(bindingParentDirPath);
        return 0;
    }

    snprintf(searchPath, searchPathLength, "%s/%s", bindingParentDirPath, WIFI_SCRIPT);

    FILE *scriptFileHost = fopen(searchPath, "r");
    if(scriptFileHost){
        AFB_INFO("searchPath = %s",searchPath);
        snprintf(buffer, size, "%s", searchPath);

        free(binderRootDirPath);
        free(bindingParentDirPath);

        return 1;
    }

    snprintf(searchPath, searchPathLength, "%s/%s", binderRootDirPath, WIFI_SCRIPT);

    FILE *scriptFileTarget = fopen(searchPath, "r");
    if(scriptFileTarget){
        AFB_INFO("searchPath = %s",searchPath);
        snprintf(buffer, size, "%s", searchPath);

        free(binderRootDirPath);
        free(bindingParentDirPath);

        return 1;
    }
    return -1;
}



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


static int GenerateHostApConfFile(wifiApT *wifiApData){

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
            (char *)wifiApData->ssid,
            wifiApData->channelNumber,
            wifiApData->maxNumberClient,
            (char *)wifiApData->countryCode,
            !wifiApData->discoverable);
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
    switch (wifiApData->securityProtocol)
    {
        case WIFI_AP_SECURITY_NONE:
            AFB_DEBUG("LE_WIFIAP_SECURITY_NONE");
            result = writeApConfigFile(HOSTAPD_CONFIG_SECURITY_NONE, configFile);
            break;

        case WIFI_AP_SECURITY_WPA2:
            AFB_DEBUG("LE_WIFIAP_SECURITY_WPA2");
            if ('\0' != wifiApData->passphrase[0])
            {
                snprintf(tmpConfig, sizeof(tmpConfig), (HOSTAPD_CONFIG_SECURITY_WPA2
                        "wpa_passphrase=%s\n"), wifiApData->passphrase);
                tmpConfig[TEMP_STRING_MAX_BYTES - 1] = '\0';
                result = writeApConfigFile(tmpConfig, configFile);
            }
            else if ('\0' != wifiApData->presharedKey[0])
            {
                snprintf(tmpConfig, sizeof(tmpConfig), (HOSTAPD_CONFIG_SECURITY_WPA2
                        "wpa_psk=%s\n"), wifiApData->presharedKey);
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
    switch( wifiApData->IeeeStdMask & HARDWARE_MODE_MASK )
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

    if ( wifiApData->IeeeStdMask & WIFI_AP_BITMASK_IEEE_STD_D )
    {
        utf8_Append(tmpConfig, "ieee80211d=1\n", sizeof(tmpConfig), NULL);
    }
    if ( wifiApData->IeeeStdMask & WIFI_AP_BITMASK_IEEE_STD_H )
    {
        utf8_Append(tmpConfig, "ieee80211h=1\n", sizeof(tmpConfig), NULL);
    }
    if ( wifiApData->IeeeStdMask & WIFI_AP_BITMASK_IEEE_STD_N )
    {
        // hw_mode=b does not support ieee80211n, but driver can handle it
        utf8_Append(tmpConfig, "ieee80211n=1\n", sizeof(tmpConfig), NULL);
    }
    if ( wifiApData->IeeeStdMask & WIFI_AP_BITMASK_IEEE_STD_AC )
    {
        utf8_Append(tmpConfig, "ieee80211ac=1\n", sizeof(tmpConfig), NULL);
    }
    if ( wifiApData->IeeeStdMask & WIFI_AP_BITMASK_IEEE_STD_AX )
    {
        utf8_Append(tmpConfig, "ieee80211ax=1\n", sizeof(tmpConfig), NULL);
    }
    if ( wifiApData->IeeeStdMask & WIFI_AP_BITMASK_IEEE_STD_W )
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

    afb_api_t wifiAP = afb_req_get_api(req);
    wifiApT *wifiApData = (wifiApT*) afb_api_get_userdata(wifiAP);
    if (!wifiApData)
    {
        afb_req_fail(req, "wifiAp_data", "Can't get wifi access point data");
        return;
    }

    // Check that an SSID is provided before starting
    if ('\0' == wifiApData->ssid[0])
    {
        AFB_ERROR("Unable to start AP because no valid SSID provided");
        afb_req_fail(req, "failed - Bad parameter", "No valid SSID provided");
        return;
    }

    // Check channel number is properly set before starting
    if ((wifiApData->channelNumber < MIN_CHANNEL_VALUE) ||
            (wifiApData->channelNumber > MAX_CHANNEL_VALUE))
    {
        AFB_ERROR("Unable to start AP because no valid channel number provided");
        afb_req_fail(req, "failed - Bad parameter", "No valid channel number provided");
        return;
    }

    // Create hostapd.conf file in /tmp
    if (GenerateHostApConfFile(wifiApData) != 0)
    {
        afb_req_fail(req, "failed", "Failed to generate hostapd.conf");
        return;
    }
    else AFB_INFO("AP configuration file has been generated");

    char cmd[PATH_MAX];
    snprintf((char *)&cmd, sizeof(cmd), " %s %s",
                wifiApData->wifiScriptPath,
                COMMAND_WIFI_HW_START);

    systemResult = system(cmd);
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
    snprintf((char *)&cmd, sizeof(cmd), " %s %s",
                wifiApData->wifiScriptPath,
                COMMAND_WIFIAP_HOSTAPD_START);

    systemResult = system(cmd);
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
    if (!wifiApData)
    {
        afb_req_fail(req, "wifiAp_data", "Can't get wifi access point data");
        return;
    }

    // Try to delete the rule allowing the DHCP ports on WLAN. Ignore if it fails
    char cmd[PATH_MAX];
    snprintf((char *)&cmd, sizeof(cmd), " %s %s",
                wifiApData->wifiScriptPath,
                COMMAND_IPTABLE_DHCP_DELETE);

    status = system(cmd);
    if ((!WIFEXITED(status)) || (0 != WEXITSTATUS(status)))
    {
        AFB_WARNING("Deleting rule for DHCP port fails");
    }

    AFB_INFO("Deleting subnet declaration ...");

    int error = deleteSubnetDeclarationConfig(wifiApData->ip_subnet, wifiApData->ip_netmask, wifiApData->ip_ap,
                                              wifiApData->ip_start, wifiApData->ip_stop);
    if (error == -1) AFB_ERROR("Unable to open /etc/dhcp/dhcpd.conf");
    else if (error == -2) AFB_ERROR("Unable to create to create temporary config file");
    else if (error == -3) AFB_ERROR("Unable to allocate memory");
    else if (error == -4) AFB_ERROR("Unable to remove old configuration file");
    else if (error == -5) AFB_ERROR("Unable to create new configuration file");

    snprintf((char *)&cmd, sizeof(cmd), " %s %s",
                wifiApData->wifiScriptPath,
                COMMAND_WIFIAP_HOSTAPD_STOP);

    status = system(cmd);
    if ((!WIFEXITED(status)) || (0 != WEXITSTATUS(status)))
    {
        AFB_ERROR("WiFi AP Command \"%s\" Failed: (%d)",
                COMMAND_WIFIAP_HOSTAPD_STOP,
                status);
        goto onErrorExit;
    }

    snprintf((char *)&cmd, sizeof(cmd), " %s %s",
                wifiApData->wifiScriptPath,
                COMMAND_WIFI_HW_STOP);

    status = system(cmd);
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
    afb_api_t wifiAP = afb_req_get_api(req);

    wifiApT *wifiApData = (wifiApT*) afb_api_get_userdata(wifiAP);
    if (!wifiApData)
    {
        afb_req_fail(req, "wifiAp_data", "Can't get wifi access point data!");
        return;
    }

    const char *ssidPtr = json_object_get_string(ssidJ);
    if(!ssidPtr)
    {
        afb_req_fail(req,"Invalid-argument","No SSID was provided!");
        return;
    }

    ;
    size_t ssidNumElements;

    AFB_INFO("Set SSID");

    ssidNumElements = strlen(ssidPtr);

    if ((0 < ssidNumElements) && (ssidNumElements <= MAX_SSID_LENGTH))
    {
        // Store SSID to be used later during startup procedure
        memcpy(&wifiApData->ssid[0], ssidPtr, ssidNumElements);
        // Make sure there is a null termination
        wifiApData->ssid[ssidNumElements] = '\0';
        AFB_INFO("SSID was set successfully %ld", ssidNumElements);
        json_object_object_add(responseJ,"SSID", json_object_new_string(wifiApData->ssid));
        afb_req_success(req, responseJ, "SSID set successfully");
    }
    else
    {
        afb_req_fail_f(req, "failed - Bad parameter", "Wi-Fi - SSID length exceeds (MAX_SSID_LENGTH = %d)!", MAX_SSID_LENGTH);
    }
    return;
}

static void setPassPhrase(afb_req_t req){

    AFB_INFO("Set Passphrase");

    json_object *passphraseJ = afb_req_json(req);
    json_object *responseJ = json_object_new_object();
    afb_api_t wifiAP = afb_req_get_api(req);

    wifiApT *wifiApData = (wifiApT*) afb_api_get_userdata(wifiAP);
    if (!wifiApData)
    {
        afb_req_fail(req, "wifiAp_data", "Can't get wifi access point data");
        return;
    }


    const char  *passphrase = json_object_get_string(passphraseJ);
    if (passphrase != NULL)
    {
        size_t length = strlen(passphrase);

        if ((length >= MIN_PASSPHRASE_LENGTH) &&
            (length <= MAX_PASSPHRASE_LENGTH))
        {
            // Store Passphrase to be used later during startup procedure
            strncpy(&wifiApData->passphrase[0], &passphrase[0], length);
            // Make sure there is a null termination
            wifiApData->passphrase[length] = '\0';
            AFB_INFO("Passphrase was set successfully");
            json_object_object_add(responseJ,"Passphrase", json_object_new_string(wifiApData->passphrase));
            afb_req_success(req, responseJ, "Passphrase set successfully!");
            return;
        }
        afb_req_fail(req, "failed - Bad parameter", "Wi-Fi - PassPhrase with Invalid length ");
        return;
    }
    afb_req_fail(req, "invalid-syntax", "Missing passPhrase");
    return;

}

static void setDiscoverable(afb_req_t req){

    AFB_INFO("Set Discoverable");

    json_object *isDiscoverableJ = afb_req_json(req);
    if (!isDiscoverableJ){
        afb_req_fail(req, "invalid-syntax", "Missing parameter");
        return;
    }
    json_object *responseJ = json_object_new_object();
    afb_api_t wifiAP = afb_req_get_api(req);

    wifiApT *wifiApData = (wifiApT*) afb_api_get_userdata(wifiAP);
    if (!wifiApData)
    {
        afb_req_fail(req, "wifiAp_data", "Can't get wifi access point data");
        return;
    }

    wifiApData->discoverable = json_object_get_boolean(isDiscoverableJ);

    AFB_INFO("AP is set as discoverable");
    json_object_object_add(responseJ,"isDiscoverable", json_object_new_boolean(wifiApData->discoverable));
    afb_req_success(req, responseJ, "AP discoverability was set successfully");

    return;
}

static void setIeeeStandard(afb_req_t req){

    afb_api_t wifiAP = afb_req_get_api(req);

    json_object *IeeeStandardJ = afb_req_json(req);
    if (!IeeeStandardJ){
        afb_req_fail(req, "invalid-syntax", "Missing parameter");
        return;
    }

    json_object *responseJ = json_object_new_object();
    int stdMask = json_object_get_int(IeeeStandardJ);


    wifiApT *wifiApData = (wifiApT*) afb_api_get_userdata(wifiAP);
    if (!wifiApData)
    {
        afb_req_fail(req, "wifiAp_data", "Can't get wifi access point data");
        return;
    }

    int8_t hwMode = (int8_t) (stdMask & 0x0F);
    int8_t numCheck = (int8_t)((hwMode & 0x1) + ((hwMode >> 1) & 0x1) +
                       ((hwMode >> 2) & 0x1) + ((hwMode >> 3) & 0x1));

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
    if (!wifiApData)
    {
        afb_req_fail(req, "wifiAp_data", "Can't get wifi access point data");
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

    afb_api_t wifiAP = afb_req_get_api(req);
    wifiApT *wifiApData = (wifiApT*) afb_api_get_userdata(wifiAP);
    if (!wifiApData)
    {
        afb_req_fail(req, "wifiAp_data", "Can't get wifi access point data");
        return;
    }

    json_object *channelNumberJ = afb_req_json(req);
    if (!channelNumberJ){
        afb_req_fail(req, "invalid-syntax", "Missing parameter");
        return;
    }

    json_object *responseJ = json_object_new_object();

    uint16_t channelNumber = (uint16_t)json_object_get_int(channelNumberJ);

    int8_t hwMode = wifiApData->IeeeStdMask & 0x0F;

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
        wifiApData->channelNumber = channelNumber;
        AFB_INFO("Channel number was set successfully");
        json_object_object_add(responseJ,"channelNumber", json_object_new_int(wifiApData->channelNumber));
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
    if (!securityProtocolJ){
        afb_req_fail(req, "invalid-syntax", "Missing parameter");
        return;
    }

    const char * securityProtocol = json_object_get_string(securityProtocolJ);
    afb_api_t wifiAP = afb_req_get_api(req);

    wifiApT *wifiApData = (wifiApT*) afb_api_get_userdata(wifiAP);
    if (!wifiApData)
    {
        afb_req_fail(req, "wifiAp_data", "Can't get wifi access point data");
        return;
    }

    if (!strcasecmp(securityProtocol,"none")) {
        wifiApData->securityProtocol = WIFI_AP_SECURITY_NONE;
        afb_req_success(req,NULL,"Security parameter was set to none!");
        return;
    }
    else if (!strcasecmp(securityProtocol,"WPA2")){
        wifiApData->securityProtocol = WIFI_AP_SECURITY_WPA2;
        afb_req_success(req,NULL,"Security parameter was set to WPA2!");
        return;
    }
    else
    {
        afb_req_fail(req, "Bad-Parameter", "Parameter is invalid!");
        return;
    }
}

static void SetPreSharedKey(afb_req_t req){

    AFB_INFO("Set preSharedKey");
    json_object *preSharedKeyJ = afb_req_json(req);
    if (!preSharedKeyJ){
        afb_req_fail(req, "invalid-syntax", "Missing parameter");
        return;
    }

    const char * preSharedKey = json_object_get_string(preSharedKeyJ);
    json_object *responseJ = json_object_new_object();
    afb_api_t wifiAP = afb_req_get_api(req);

    wifiApT *wifiApData = (wifiApT*) afb_api_get_userdata(wifiAP);
    if (!wifiApData)
    {
        afb_req_fail(req, "wifiAp_data", "Can't get wifi access point data");
        return;
    }

    if (preSharedKey != NULL)
    {
        uint32_t length = (uint32_t) strlen(preSharedKey);

        if (length <= MAX_PSK_LENGTH)
        {
            // Store PSK to be used later during startup procedure
            utf8_Copy(wifiApData->presharedKey, preSharedKey, sizeof(wifiApData->presharedKey), NULL);

            AFB_INFO("PreSharedKey was set successfully to %s",wifiApData->presharedKey);
            json_object_object_add(responseJ,"preSharedKey", json_object_new_string(wifiApData->presharedKey));
            afb_req_success(req,responseJ,"PreSharedKey was set successfully!");
            return;
        }
        else
        {
            afb_req_fail(req, "Bad-Parameter", "Parameter length is invalid!");
            return;
        }
    }
}

static void setCountryCode(afb_req_t req){

    AFB_INFO("Set country code");
    json_object *countryCodeJ = afb_req_json(req);
    if (!countryCodeJ){
        afb_req_fail(req, "invalid-syntax", "Missing parameter");
        return;
    }


    const char * countryCode = json_object_get_string(countryCodeJ);
    json_object *responseJ = json_object_new_object();

    afb_api_t wifiAP = afb_req_get_api(req);

    wifiApT *wifiApData = (wifiApT*) afb_api_get_userdata(wifiAP);
    if (!wifiApData)
    {
        afb_req_fail(req, "wifiAp_data", "Can't get wifi access point data");
        return;
    }

    if (countryCode != NULL)
    {
        uint32_t length = (uint32_t) strlen(countryCode);

        if (length == ISO_COUNTRYCODE_LENGTH)
        {
            strncpy(&wifiApData->countryCode[0], &countryCode[0], length );
            wifiApData->countryCode[length] = '\0';

            AFB_INFO("country code was set to %s",wifiApData->countryCode);
            json_object_object_add(responseJ,"countryCode", json_object_new_string(wifiApData->countryCode));
            afb_req_success(req,responseJ,"country code was set successfully");
            return;
        }
        else
        {
            afb_req_fail(req, "Bad-Parameter", "Parameter length is invalid!");
            return;
        }
    }
    else afb_req_fail(req, "Bad-Parameter", "Parameter is invalid");
    return;

}

static void SetMaxNumberClients(afb_req_t req){

    AFB_INFO("Set the maximum number of clients");

    json_object *maxNumberClientsJ = afb_req_json(req);
    if (!maxNumberClientsJ){
        afb_req_fail(req, "invalid-syntax", "Missing parameter");
        return;
    }
    json_object *responseJ = json_object_new_object();
    int maxNumberClients = json_object_get_int(maxNumberClientsJ);

    afb_api_t wifiAP = afb_req_get_api(req);

    wifiApT *wifiApData = (wifiApT*) afb_api_get_userdata(wifiAP);
    if (!wifiApData)
    {
        afb_req_fail(req, "wifiAp_data", "Can't get wifi access point data");
        return;
    }

    if ((maxNumberClients >= 1) && (maxNumberClients <= WIFI_AP_MAX_USERS))
    {
       wifiApData->maxNumberClient = maxNumberClients;

       AFB_NOTICE("The maximum number of clients was set to %d",wifiApData->maxNumberClient);
       json_object_object_add(responseJ,"maxNumberClients", json_object_new_int(wifiApData->maxNumberClient));
       afb_req_success(req,responseJ,"Max Number of clients was set successfully!");
       return;
    }
    else afb_req_fail(req, "Bad-Parameter", "The value is out of range");
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
    if (!wifiApData)
    {
        afb_req_fail(req, "wifiAp_data", "Can't get wifi access point data");
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

    // get ip address with CIDR annotation
    int netmask_cidr = toCidr(ip_netmask);
    char ip_ap_cidr[128] ;
    snprintf((char *)&ip_ap_cidr, sizeof(ip_ap_cidr), "%s/%d",
                    ip_ap,
                    netmask_cidr);

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

        AFB_INFO("@AP=%x, @APstart=%x, @APstop=%x, @APnetmask=%x @APnetid=%x @AP_CIDR=%s",
                ap, start, stop, netmask, subnet, ip_ap_cidr);

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
        char cmd[PATH_MAX];
        int  systemResult;

        snprintf((char *)&cmd, sizeof(cmd), " %s %s %s",
                wifiApData->wifiScriptPath,
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
                    fprintf(filePtrTmp, DHCP_CONFIG_COMMON);
                    fprintf(filePtrTmp, "subnet %s netmask %s {\n",ip_subnet ,ip_netmask);
                    fprintf(filePtrTmp, "option routers %s;\n",ip_ap);
                    fprintf(filePtrTmp, "option subnet-mask %s;\n",ip_netmask);
                    fprintf(filePtrTmp, "option domain-search    \"iotbzh.lan\";\n");
                    fprintf(filePtrTmp, "range %s %s;}\n", ip_start, ip_stop);
                    fclose(filePtrTmp);
                }
                else
                {
                    AFB_ERROR("Unable to open the dhcp configuration file: %m.");
                    goto OnErrorExit;
                }
            }
            else
            {
                FILE *filePtr;

                filePtr = fopen (DHCP_CFG_LINK, "a");

                AFB_INFO("Update dhcp configuration file (%s)", DHCP_CFG_LINK);
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
            snprintf((char *)&cmd, sizeof(cmd), " %s %s",
                wifiApData->wifiScriptPath,
                COMMAND_IPTABLE_DHCP_INSERT);

            systemResult = system(cmd);

            if (WEXITSTATUS (systemResult) != 0)
            {
                AFB_ERROR("Unable to allow DHCP ports.");
                goto OnErrorExit;
            }

            char cmd[PATH_MAX];
            snprintf((char *)&cmd, sizeof(cmd), "%s %s %s",
                    wifiApData->wifiScriptPath,
                    COMMAND_DHCP_RESTART,
                    ip_ap_cidr);

            systemResult = system(cmd);
            if (WEXITSTATUS (systemResult) != 0)
            {
                AFB_ERROR("Unable to restart the DHCP server.");
                goto OnErrorExit;
            }
        }
    }

    size_t ipAddressNumElements;

    ipAddressNumElements = strlen(ip_ap);

    if ((0 < ipAddressNumElements) && (ipAddressNumElements <= MAX_IP_ADDRESS_LENGTH))
    {
        // Store ip address of AP to be used later during cleanup procedure
        utf8_Copy(wifiApData->ip_ap, ip_ap, sizeof(wifiApData->ip_ap), NULL);

        // Store ip address of subnet to be used later during cleanup procedure
        utf8_Copy(wifiApData->ip_subnet, ip_subnet, sizeof(wifiApData->ip_subnet), NULL);

        // Store ip address of the netmask to be used later during cleanup procedure
        utf8_Copy(wifiApData->ip_netmask, ip_netmask, sizeof(wifiApData->ip_netmask), NULL);

        // Store AP range start ip address to be used later during cleanup procedure
        utf8_Copy(wifiApData->ip_start, ip_start, sizeof(wifiApData->ip_start), NULL);

        // Store AP range stop ip address to be used later during cleanup procedure
        utf8_Copy(wifiApData->ip_stop, ip_stop, sizeof(wifiApData->ip_stop), NULL);
    }
    else
    {
        afb_req_fail(req, "failed - Bad parameter", "Wi-Fi - ip address invalid");
        return;
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

int wifiApConfig(afb_api_t apiHandle, CtlSectionT *section, json_object *wifiApConfigJ)
{

    char *uid, *ssid , *securityProtocol ,*passphrase ,*countryCode;

    wifiApT *wifiApData = (wifiApT*) afb_api_get_userdata(apiHandle);
    if (!wifiApData)
    {
        return -1;
    }

    char script_path[4096] = "";
    int res = getScriptPath(apiHandle, script_path, sizeof script_path);
	if (res < 0 || (int)res >= (int)(sizeof script_path))
	{
		return -2;
	}


    strcpy(wifiApData->wifiScriptPath, script_path);

    AFB_API_INFO(apiHandle, "%s , %s", __func__,json_object_get_string(wifiApConfigJ));

    int error = wrap_json_unpack(wifiApConfigJ, "{s?s,ss,s?s,s?i,s?b,si,ss,s?s,s?s,s?i}"
            , "uid"              , &uid
            , "interfaceName"    , &wifiApData->interfaceName
            , "ssid"             , &ssid
            , "channelNumber"    , &wifiApData->channelNumber
            , "discoverable"     , &wifiApData->discoverable
            , "IeeeStdMask"      , &wifiApData->IeeeStdMask
            , "securityProtocol" , &securityProtocol
            , "passphrase"       , &passphrase
            , "countryCode"      , &countryCode
            , "maxNumberClient"  , &wifiApData->maxNumberClient
            );
    if (error) {
		AFB_API_ERROR(apiHandle, "%s: invalid-syntax error=%s args=%s",
				__func__, wrap_json_get_error_string(error), json_object_get_string(wifiApConfigJ));
        return -3;
    }

    // make sure string do not get deleted

    wifiApData->interfaceName = strdup(wifiApData->interfaceName);
	if (wifiApData->interfaceName == NULL) {
		return -4;
	}

	if (wifiApData->ssid) {
        utf8_Copy(wifiApData->ssid, ssid, sizeof(wifiApData->ssid), NULL);
		if (wifiApData->ssid == NULL) {
			return -5;
		}
	}

    if (wifiApData->passphrase) {
        utf8_Copy(wifiApData->passphrase, passphrase, sizeof(wifiApData->passphrase), NULL);
		if (wifiApData->passphrase == NULL) {
			return -6;
		}
	}

    if (wifiApData->countryCode) {
        utf8_Copy(wifiApData->countryCode, countryCode, sizeof(wifiApData->countryCode), NULL);
		if (wifiApData->countryCode == NULL) {
			return -7;
		}
	}

    if (securityProtocol) {
		if (!strcasecmp(securityProtocol,"none")) {
        wifiApData->securityProtocol = WIFI_AP_SECURITY_NONE;
        }
        else if (!strcasecmp(securityProtocol,"WPA2")){
            wifiApData->securityProtocol = WIFI_AP_SECURITY_WPA2;
        }
        if (!wifiApData->securityProtocol){
            return -8;
        }
	}

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
 *		      WiFi Access Point Controller verbs table    	                   *
 *******************************************************************************/

static CtlSectionT ctlSections[]= {
	{ .key = "config",          .loadCB = wifiApConfig },
    {.key=NULL}
};


/*******************************************************************************
 *                     pre-Initialize the binding                              *
 ******************************************************************************/

static CtlConfigT *init_wifi_AP_controller(afb_api_t apiHandle)
{
	int index;
	char *dirList, *fileName, *fullPath;
	char filePath[255];

	filePath[255 - 1] = '\0';

    int err;
	CtlConfigT *ctrlConfig;
	CtlSectionT *ctrlCurrentSections;

    json_object *configJ, *entryJ;

    AFB_API_NOTICE (apiHandle, "Controller in Binding pre-init");

    // check if config file exist
    dirList= getenv("CTL_CONFIG_PATH");
    if (!dirList) dirList = GetDefaultConfigSearchPath(apiHandle);

    AFB_API_DEBUG(apiHandle, "Controller configuration files search path : %s", dirList);

    // Select correct config file
    char *configPath = CtlConfigSearch(apiHandle, dirList, "wifi");
    AFB_API_DEBUG(apiHandle, "Controller configuration files search  : %s", configPath);

    configJ = CtlConfigScan(dirList, "wifi");
	if(! configJ) {
		AFB_API_WARNING(apiHandle, "No config file(s) found in %s", dirList);
		// return ctrlConfig;
	}


    // We load 1st file others are just warnings
	for(index = 0; index < (int) json_object_array_length(configJ); index++) {
		entryJ = json_object_array_get_idx(configJ, index);

		if(wrap_json_unpack(entryJ, "{s:s, s:s !}", "fullpath", &fullPath, "filename", &fileName)) {
			AFB_API_ERROR(apiHandle, "Invalid JSON entry = %s", json_object_get_string(entryJ));
			// return ctrlConfig;
		}
        AFB_API_INFO(apiHandle, " JSON  = %s", json_object_get_string(entryJ));

		strncpy(filePath, fullPath, sizeof(filePath) - 1);
		strncat(filePath, "/", sizeof(filePath) - 1);
		strncat(filePath, fileName, sizeof(filePath) - 1);


	}

    // Select correct config file
    ctrlConfig = CtlLoadMetaData(apiHandle, configPath);
    if (!ctrlConfig) {
        AFB_API_ERROR(apiHandle, "CtrlBindingDyn No valid control config file in:\n-- %s", configPath);
        goto OnErrorExit;
    }

    if (!ctrlConfig->api) {
        AFB_API_ERROR(apiHandle, "CtrlBindingDyn API Missing from metadata in:\n-- %s", configPath);
        goto OnErrorExit;
    }

    AFB_API_NOTICE (apiHandle, "Controller API='%s' info='%s'", ctrlConfig->api, ctrlConfig->info);

    ctrlCurrentSections = malloc(sizeof(ctlSections));
	if(! ctrlCurrentSections) {
		AFB_API_ERROR(apiHandle, "Didn't succeed to allocate current internal hal section data structure for controller");
		return ctrlConfig;
	}

	memcpy(ctrlCurrentSections, ctlSections, sizeof(ctlSections));

	// Load section for corresponding Api
	err = CtlLoadSections(apiHandle, ctrlConfig, ctrlCurrentSections);
	if(err < 0) {
		AFB_API_ERROR(apiHandle, "Error %i caught when trying to load current config controller sections", err);
		return ctrlConfig;
	}

	if(err > 0)
		AFB_API_WARNING(apiHandle, "Warning %i raised when trying to load current wifi controller sections", err);

	return ctrlConfig;
OnErrorExit:
    return ctrlConfig;
}
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

    //initWifiApData(api, wifiApData);
	if(! wifiApData)
		return -4;

    afb_api_set_userdata(api, wifiApData);
    cds_list_add_tail(&wifiApData->wifiApListHead, &wifiApList);

    CtlConfigT *ctrlConfig = init_wifi_AP_controller(api);
    if (!ctrlConfig) return -5;

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