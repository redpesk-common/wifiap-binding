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

#include "wifi-ap-data.h"

#include <string.h>

#define AFB_BINDING_VERSION 4
#include <afb/afb-binding.h>

#include "wifi-ap-utilities.h"

/*******************************************************************************
 * set new copy string to the destination, freeing previous value              *
 * @return                                                                     *
 *     * WIFIAP_NO_ERROR if function succeeded                                 *
 *     * WIFIAP_ERROR_INVALID if invalid src                                   *
 *     * WIFIAP_ERROR_TOO_SMALL if too small src                               *
 *     * WIFIAP_ERROR_OOM if out of memory                                     *
 ******************************************************************************/
static int set_string_copy(char **dest, const char *src)
{
    char *new, *old;

    if (src == NULL)
        return WIFIAP_ERROR_INVALID;

    if (src[0] == 0)
        return WIFIAP_ERROR_TOO_SMALL;

    new = strdup(src);
    if (new == NULL)
        return WIFIAP_ERROR_OOM;

    old = *dest;
    *dest = new;
    free(old);

    return WIFIAP_NO_ERROR;
}

/*******************************************************************************
 * set a string buffer                                                         *
 *******************************************************************************
 * @return                                                                     *
 *     * WIFIAP_ERROR_INVALID if src is invalid (NULL)                         *
 *     * WIFIAP_ERROR_TOO_SMALL if src is too small                            *
 *     * WIFIAP_ERROR_TOO_LONG if src is too long                              *
 *     * WIFIAP_NO_ERROR if function succeeded                                 *
 ******************************************************************************/
static int set_buffer(char *dest, const char *src, size_t minlen, size_t maxlen)
{
    size_t len;

    if (src == NULL)
        return WIFIAP_ERROR_INVALID;

    len = strlen(src);
    if (len < minlen)
        return WIFIAP_ERROR_TOO_SMALL;

    if (len > maxlen)
        return WIFIAP_ERROR_TOO_LONG;

    memcpy(dest, src, len + 1);
    return WIFIAP_NO_ERROR;
}

/*******************************************************************************
 *     Set the host name                                                       *
 * @return                                                                     *
 *     * WIFIAP_NO_ERROR if function succeeded                                 *
 *     * WIFIAP_ERROR_INVALID if invalid host name                             *
 *     * WIFIAP_ERROR_TOO_SMALL if too small host name                         *
 *     * WIFIAP_ERROR_OOM if out of memory                                     *
 ******************************************************************************/
int setHostNameParameter(wifiApT *wifiApData, const char *hostName)
{
    return set_string_copy(&wifiApData->hostName, hostName);
}

/*******************************************************************************
 *     Set the domain name                                                     *
 * @return                                                                     *
 *     * WIFIAP_NO_ERROR if function succeeded                                 *
 *     * WIFIAP_ERROR_INVALID if invalid domain name                           *
 *     * WIFIAP_ERROR_TOO_SMALL if too small domain name                       *
 *     * WIFIAP_ERROR_OOM if out of memory                                     *
 ******************************************************************************/
int setDomainNameParameter(wifiApT *wifiApData, const char *domainName)
{
    return set_string_copy(&wifiApData->domainName, domainName);
}

/*******************************************************************************
 *     Set the interface name                                                  *
 * @return                                                                     *
 *     * WIFIAP_NO_ERROR if function succeeded                                 *
 *     * WIFIAP_ERROR_INVALID if invalid interface name                        *
 *     * WIFIAP_ERROR_TOO_SMALL if too small interface name                    *
 *     * WIFIAP_ERROR_OOM if out of memory                                     *
 ******************************************************************************/
int setInterfaceNameParameter(wifiApT *wifiApData, const char *interfaceName)
{
    return set_string_copy(&wifiApData->interfaceName, interfaceName);
}

/*******************************************************************************
 *               set the wifi access point SSID                                *
 * @return                                                                     *
 *     * WIFIAP_ERROR_INVALID if ssid is invalid (NULL)                        *
 *     * WIFIAP_ERROR_TOO_SMALL if ssid is too small                           *
 *     * WIFIAP_ERROR_TOO_LONG if ssid is too long                             *
 *     * WIFIAP_NO_ERROR if function succeeded                                 *
 ******************************************************************************/
int setSsidParameter(wifiApT *wifiApData, const char *ssid)
{
    return set_buffer(wifiApData->ssid, ssid, MIN_SSID_LENGTH, MAX_SSID_LENGTH);
}

/*******************************************************************************
 *               set the number of wifi access point channel                   *
 ******************************************************************************/
int setChannelParameter(wifiApT *wifiApData, int channelNumber)
{
    int8_t hwMode = wifiApData->IeeeStdMask & 0x0F;

    switch (hwMode) {
    case WIFI_AP_BITMASK_IEEE_STD_A:
        wifiApData->channel.MIN_CHANNEL_VALUE = MIN_CHANNEL_STD_A;
        wifiApData->channel.MAX_CHANNEL_VALUE = MAX_CHANNEL_STD_A;
        break;
    case WIFI_AP_BITMASK_IEEE_STD_B:
    case WIFI_AP_BITMASK_IEEE_STD_G:
        wifiApData->channel.MIN_CHANNEL_VALUE = MIN_CHANNEL_VALUE_DEF;
        wifiApData->channel.MAX_CHANNEL_VALUE = MAX_CHANNEL_VALUE_DEF;
        break;
    case WIFI_AP_BITMASK_IEEE_STD_AD:
        wifiApData->channel.MIN_CHANNEL_VALUE = MIN_CHANNEL_STD_AD;
        wifiApData->channel.MAX_CHANNEL_VALUE = MAX_CHANNEL_STD_AD;
        break;
    default:
        AFB_WARNING("Invalid hardware mode");
    }

    if ((channelNumber >= (int)wifiApData->channel.MIN_CHANNEL_VALUE) &&
        (channelNumber <= (int)wifiApData->channel.MAX_CHANNEL_VALUE)) {
        wifiApData->channelNumber = (uint16_t)channelNumber;
        return 1;
    }
    return 0;
}

/*******************************************************************************
 *           set the IEEE standard to use for the access point                 *
 * @return
 *     - WIFIAP_NO_ERROR no error
 *     - WIFIAP_ERROR_NO_HARD no hardware bit set
 *     - WIFIAP_ERROR_MANY_HARD more than one hardware bit set
 ******************************************************************************/

int setIeeeStandardParameter(wifiApT *wifiApData, int stdMask)
{
    int check = stdMask & (WIFI_AP_BITMASK_IEEE_STD_A
                          |WIFI_AP_BITMASK_IEEE_STD_B
                          |WIFI_AP_BITMASK_IEEE_STD_G
                          |WIFI_AP_BITMASK_IEEE_STD_AD);

    // Hardware mode should be set.
    if (check == 0)
        return WIFIAP_ERROR_NO_HARD;

    // Hardware mode should be exclusive.
    if ((check & (check - 1)) != 0)
        return WIFIAP_ERROR_MANY_HARD;

    // ieee80211ac=1 only works with hw_mode=a
    if ((stdMask & WIFI_AP_BITMASK_IEEE_STD_AC) != 0
     && (stdMask & WIFI_AP_BITMASK_IEEE_STD_A) == 0)
        return WIFIAP_ERROR_STD_AC;

    // ieee80211h=1 can be used only with ieee80211d=1
    if ((stdMask & WIFI_AP_BITMASK_IEEE_STD_H) != 0
     && (stdMask & WIFI_AP_BITMASK_IEEE_STD_D) == 0)
        return WIFIAP_ERROR_STD_H;

    wifiApData->IeeeStdMask = stdMask;
    return WIFIAP_NO_ERROR;
}

/*******************************************************************************
 *                     set access point passphrase                             *
 * @return                                                                     *
 *     * WIFIAP_ERROR_INVALID if passphrase is invalid (NULL)                  *
 *     * WIFIAP_ERROR_TOO_SMALL if passphrase is too small                     *
 *     * WIFIAP_ERROR_TOO_LONG if passphrase is too long                       *
 *     * WIFIAP_NO_ERROR if function succeeded                                 *
 ******************************************************************************/
int setPassPhraseParameter(wifiApT *wifiApData, const char *passphrase)
{
    return set_buffer(wifiApData->passphrase, passphrase, MIN_PASSPHRASE_LENGTH, MAX_PASSPHRASE_LENGTH);
}

/*******************************************************************************
 *                     set access point pre-shared key                         *
 * @return                                                                     *
 *     * -1 if preshared key is of invalid length                              *
 *     *  0 if function succeeded                                              *
 ******************************************************************************/
int setPreSharedKeyParameter(wifiApT *wifiApData, const char *preSharedKey)
{
    uint32_t length = (uint32_t)strlen(preSharedKey);

    if (length <= MAX_PSK_LENGTH) {
        // Store PSK to be used later during startup procedure
        utf8_Copy(wifiApData->presharedKey, preSharedKey, sizeof(wifiApData->presharedKey), NULL);
        return 0;
    }
    return -1;
}

/*******************************************************************************
 *                     set access point security protocol                      *
 * @return                                                                     *
 *     * -1 if security protocol is invalid                                    *
 *     *  0 if security protocol is successfully set to none                   *
 *     *  1 if security protocol is successfully set to WPA2                   *
 ******************************************************************************/
int setSecurityProtocolParameter(wifiApT *wifiApData, const char *securityProtocol)
{
    if (!strcasecmp(securityProtocol, "none")) {
        wifiApData->securityProtocol = WIFI_AP_SECURITY_NONE;
        return 0;
    }
    else if (!strcasecmp(securityProtocol, "WPA2")) {
        wifiApData->securityProtocol = WIFI_AP_SECURITY_WPA2;
        return 1;
    }
    return -1;
}

/*******************************************************************************
 *               set the country code to use for access point                  *
 * @return                                                                     *
 *     * -1 if country code is of invalid length                               *
 *     *  0 if function succeeded                                              *
 ******************************************************************************/
int setCountryCodeParameter(wifiApT *wifiApData, const char *countryCode)
{
    uint32_t length = (uint32_t)strlen(countryCode);

    if (length == ISO_COUNTRYCODE_LENGTH) {
        memcpy(&wifiApData->countryCode[0], &countryCode[0], length);
        wifiApData->countryCode[length] = '\0';
        return 0;
    }
    return -1;
}

/*******************************************************************************
 *               set the max number of clients of access point                 *
 * @return                                                                     *
 *     * -1 if the maximum number of clients is out of range                   *
 *     *  0 if function succeeded                                              *
 ******************************************************************************/
int setMaxNumberClients(wifiApT *wifiApData, int maxNumberClients)
{
    if ((maxNumberClients >= 1) && (maxNumberClients <= WIFI_AP_MAX_USERS)) {
        wifiApData->maxNumberClient = (uint32_t)maxNumberClients;
        return 0;
    }
    return -1;
}

/*******************************************************************************
 *     Set the access point IP address and client IP  addresses rang           *
 ******************************************************************************/
int setIpRangeParameters(wifiApT *wifiApData,
                         const char *ip_ap,
                         const char *ip_start,
                         const char *ip_stop,
                         const char *ip_netmask)
{
    size_t ipAddressNumElements;

    ipAddressNumElements = strlen(ip_ap);

    if ((0 < ipAddressNumElements) && (ipAddressNumElements <= MAX_IP_ADDRESS_LENGTH)) {
        // Store ip address of AP to be used later during cleanup procedure
        utf8_Copy(wifiApData->ip_ap, ip_ap, sizeof(wifiApData->ip_ap), NULL);

        // Store AP range start ip address to be used later during cleanup procedure
        utf8_Copy(wifiApData->ip_start, ip_start, sizeof(wifiApData->ip_start), NULL);

        // Store AP range stop ip address to be used later during cleanup procedure
        utf8_Copy(wifiApData->ip_stop, ip_stop, sizeof(wifiApData->ip_stop), NULL);

        // Store AP range netmasq ip address to be used later during cleanup procedure
        utf8_Copy(wifiApData->ip_netmask, ip_netmask, sizeof(wifiApData->ip_netmask), NULL);
    }
    else {
        goto OnErrorExit;
    }

    return 0;
OnErrorExit:
    return -1;
}
