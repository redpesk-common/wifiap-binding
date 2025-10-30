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

#include "wifi-ap-config.h"

#include <string.h>

#define AFB_BINDING_VERSION 4
#include <afb/afb-binding.h>

#include "wifi-ap-utilities.h"

/*******************************************************************************
 *      Create access point specific hosts configuration file                  *
 *                                                                             *
 * @return                                                                     *
 *      0 if success, or -1 if not.                                            *
 ******************************************************************************/

int createHostsConfigFile(const char *ip_ap, char *hostName)
{
    const char *configFileName = "/tmp/add_hosts";

    FILE *ConfigFile = fopen(configFileName, "w");
    if (!ConfigFile) {
        fclose(ConfigFile);
        return -1;
    }

    if (ConfigFile != NULL) {
        // set a hostname for the access point
        fprintf(ConfigFile, "%s %s\n", ip_ap, hostName);
        fclose(ConfigFile);
    }
    else {
        AFB_ERROR("Unable to open the dnsmasq configuration file: %m.");
        return -2;
    }

    return 0;
}

/*******************************************************************************
 *  Create access point specific polkit Network Manager permission rules file  *
 *                                                                             *
 * @return                                                                     *
 *      0 if success, or -1 if not.                                            *
 ******************************************************************************/

int createPolkitRulesFile_NM()
{
    FILE *ConfigFile = fopen(WIFI_POLKIT_NM_CONF_FILE, "w");

    if (!ConfigFile) {
        fclose(ConfigFile);
        return -1;
    }

    if (ConfigFile != NULL) {
        // Interface is generated when COMMAND_WIFI_NM_UNMANAGE called
        fprintf(ConfigFile, "%s\n", POLKIT_NM_CONFIG_RULES);
        fclose(ConfigFile);
    }
    else {
        printf("Unable to open the polkit Network Manager rules file: %m.");
        return -2;
    }
    return 0;
}

/*******************************************************************************
 *     Create access point specific polkit Firewalld permission rules file     *
 *                                                                             *
 * @return                                                                     *
 *      0 if success, or -1 if not.                                            *
 ******************************************************************************/

int createPolkitRulesFile_Firewalld()
{
    FILE *ConfigFile = fopen(WIFI_POLKIT_FIREWALLD_CONF_FILE, "w");

    if (!ConfigFile) {
        fclose(ConfigFile);
        return -1;
    }

    if (ConfigFile != NULL) {
        // Interface is generated when COMMAND_WIFI_FIREWALLD_ALLOW called
        fprintf(ConfigFile, "%s\n", POLKIT_FIREWALLD_CONFIG_RULES);
        fclose(ConfigFile);
    }
    else {
        printf("Unable to open the polkit firewalld rules file: %m.");
        return -2;
    }
    return 0;
}

/*******************************************************************************
 *      Create access point specific DNSMASQ configuration file                *
 *                                                                             *
 * @return                                                                     *
 *      0 if success, or -1 if not.                                            *
 ******************************************************************************/

int createDnsmasqConfigFile(const char *ip_ap,
                            const char *ip_start,
                            const char *ip_stop,
                            char *domainName)
{
    const char *configFileName = "/tmp/dnsmasq.wlan.conf";

    FILE *ConfigFile = fopen(configFileName, "w");
    if (!ConfigFile) {
        fclose(ConfigFile);
        return -1;
    }

    if (ConfigFile != NULL) {
        // Interface is generated when COMMAND_DNSMASQ_RESTART called
        fprintf(ConfigFile, "bind-interfaces\nlisten-address=%s\n", ip_ap);
        fprintf(
            ConfigFile,
            "expand-hosts\naddn-hosts=/tmp/add_hosts\ndomain=%s\nlocal=/%s/\n",
            domainName, domainName);
        fprintf(ConfigFile, "dhcp-range=%s,%s,%dh\n", ip_start, ip_stop, 24);
        fprintf(ConfigFile, "dhcp-option=%d,%s\n", 3, ip_ap);
        fprintf(ConfigFile, "dhcp-option=%d,%s\n", 6, ip_ap);
        fclose(ConfigFile);
    }
    else {
        AFB_ERROR("Unable to open the dnsmasq configuration file: %m.");
        return -2;
    }

    return 0;
}

/*******************************************************************************
 *                   Generate hostapd configuration file                       *
 *                                                                             *
 * @return                                                                     *
 *      0 if success, or -1 if not.                                            *
 ******************************************************************************/

int GenerateHostApConfFile(wifiApT *wifiApData)
{
    char tmpConfig[TEMP_STRING_MAX_BYTES];
    FILE *configFile = NULL;
    int result = -1;

    configFile = fopen(WIFI_HOSTAPD_FILE, "w");
    if (NULL == configFile) {
        AFB_ERROR("Unable to create hostapd.conf file");
        return -1;
    }
    else
        AFB_INFO("hostapd.conf file created successfully");

    memset(tmpConfig, '\0', sizeof(tmpConfig));
    // prepare SSID, channel, country code etc in hostapd.conf
    snprintf(
        tmpConfig, sizeof(tmpConfig),
        (HOSTAPD_CONFIG_COMMON "ssid=%s\nchannel=%d\nmax_num_sta=%d\ncountry_"
                               "code=%s\nignore_broadcast_ssid=%d\n"),
        (char *)wifiApData->ssid, wifiApData->channelNumber,
        wifiApData->maxNumberClient, (char *)wifiApData->countryCode,
        !wifiApData->discoverable);
    // Write common config such as SSID, channel, country code, etc in
    // hostapd.conf
    tmpConfig[TEMP_STRING_MAX_BYTES - 1] = '\0';
    if (writeApConfigFile(tmpConfig, configFile) != 0) {
        AFB_ERROR("Unable to set SSID, channel, etc in hostapd.conf");
        goto error;
    }
    else
        AFB_INFO(
            "AP parameters has been set in hostapd.conf file successfully");

    memset(tmpConfig, '\0', sizeof(tmpConfig));
    // Write security parameters in hostapd.conf
    switch (wifiApData->securityProtocol) {
    case WIFI_AP_SECURITY_NONE:
        AFB_DEBUG("WIFI_AP_SECURITY_NONE");
        result = writeApConfigFile(HOSTAPD_CONFIG_SECURITY_NONE, configFile);
        break;

    case WIFI_AP_SECURITY_WPA2:
        AFB_DEBUG("WIFI_AP_SECURITY_WPA2");
        if ('\0' != wifiApData->passphrase[0]) {
            snprintf(tmpConfig, sizeof(tmpConfig),
                     (HOSTAPD_CONFIG_SECURITY_WPA2 "wpa_passphrase=%s\n"),
                     wifiApData->passphrase);
            tmpConfig[TEMP_STRING_MAX_BYTES - 1] = '\0';
            result = writeApConfigFile(tmpConfig, configFile);
        }
        else if ('\0' != wifiApData->presharedKey[0]) {
            snprintf(tmpConfig, sizeof(tmpConfig),
                     (HOSTAPD_CONFIG_SECURITY_WPA2 "wpa_psk=%s\n"),
                     wifiApData->presharedKey);
            tmpConfig[TEMP_STRING_MAX_BYTES - 1] = '\0';
            result = writeApConfigFile(tmpConfig, configFile);
        }
        else {
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
    if (result != 0) {
        AFB_ERROR("Unable to set security parameters in hostapd.conf");
        goto error;
    }
    else
        AFB_INFO(
            "Security parameters has been set successfully in hostapd.conf ");

    // prepare IEEE std including hardware mode into hostapd.conf
    memset(tmpConfig, '\0', sizeof(tmpConfig));
    switch (wifiApData->IeeeStdMask & HARDWARE_MODE_MASK) {
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

    if (wifiApData->IeeeStdMask & WIFI_AP_BITMASK_IEEE_STD_D) {
        utf8_Append(tmpConfig, "ieee80211d=1\n", sizeof(tmpConfig), NULL);
    }
    if (wifiApData->IeeeStdMask & WIFI_AP_BITMASK_IEEE_STD_H) {
        utf8_Append(tmpConfig, "ieee80211h=1\n", sizeof(tmpConfig), NULL);
    }
    if (wifiApData->IeeeStdMask & WIFI_AP_BITMASK_IEEE_STD_N) {
        // hw_mode=b does not support ieee80211n, but driver can handle it
        utf8_Append(tmpConfig, "ieee80211n=1\n", sizeof(tmpConfig), NULL);
    }
    if (wifiApData->IeeeStdMask & WIFI_AP_BITMASK_IEEE_STD_AC) {
        utf8_Append(tmpConfig, "ieee80211ac=1\n", sizeof(tmpConfig), NULL);
    }
    if (wifiApData->IeeeStdMask & WIFI_AP_BITMASK_IEEE_STD_AX) {
        utf8_Append(tmpConfig, "ieee80211ax=1\n", sizeof(tmpConfig), NULL);
    }
    if (wifiApData->IeeeStdMask & WIFI_AP_BITMASK_IEEE_STD_W) {
        utf8_Append(tmpConfig, "ieee80211w=1\n", sizeof(tmpConfig), NULL);
    }
    // Write IEEE std in hostapd.conf
    tmpConfig[TEMP_STRING_MAX_BYTES - 1] = '\0';
    if (writeApConfigFile(tmpConfig, configFile) != 0) {
        AFB_ERROR("Unable to set IEEE std in hostapd.conf");
        goto error;
    }
    else
        AFB_INFO("IEEE std has been set successfully in hostapd.conf");
    fclose(configFile);
    return 0;

error:
    fclose(configFile);
    // Remove generated hostapd.conf file
    remove(WIFI_HOSTAPD_FILE);
    return -1;
}

/*******************************************************************************
 *                       write hostapd config file                             *
 *                                                                             *
 * @return                                                                     *
 *      0 if success, or -1 if not.                                            *
 ******************************************************************************/
int writeApConfigFile(const char *data, FILE *file)
{
    size_t length;

    if ((NULL == file) || (NULL == data)) {
        AFB_ERROR("Invalid parameter(s)");
        return -1;
    }

    length = strlen(data);
    if (length > 0) {
        if (fwrite(data, 1, length, file) != length) {
            AFB_ERROR("Unable to generate the hostapd file.");
            return -1;
        }
    }

    return 0;
}
