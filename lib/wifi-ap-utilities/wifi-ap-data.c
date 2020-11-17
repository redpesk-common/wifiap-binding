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

#include <stdio.h>
#include <string.h>
#include <json-c/json.h>
#include <arpa/inet.h>

#include "wifi-ap-data.h"
#include "wifi-ap-utilities.h"
#include "wifi-ap-config.h"

/*******************************************************************************
 *               set the wifi access point SSID                                *
 ******************************************************************************/
int setSsidParameter(wifiApT *wifiApData, const char *ssid)
{
    size_t ssidNumElements;
    ssidNumElements = strlen(ssid);

    if ((0 < ssidNumElements) && (ssidNumElements <= MAX_SSID_LENGTH))
    {
        // Store SSID to be used later during startup procedure
        memcpy(&wifiApData->ssid[0], ssid, ssidNumElements);
        // Make sure there is a null termination
        wifiApData->ssid[ssidNumElements] = '\0';
        return 1;
    }
    return 0;
}

/*******************************************************************************
 *               set the number of wifi access point channel                   *
 ******************************************************************************/
int setChannelParameter(wifiApT *wifiApData, uint16_t channelNumber)
{
    int8_t hwMode = wifiApData->IeeeStdMask & 0x0F;

    switch (hwMode)
    {
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

    if ((channelNumber >= wifiApData->channel.MIN_CHANNEL_VALUE) &&
        (channelNumber <= wifiApData->channel.MAX_CHANNEL_VALUE))
    {
        wifiApData->channelNumber = channelNumber;
       return 1;
    }
    return 0;
}

/*******************************************************************************
 *           set the IEEE standard to use for the access point                 *
 ******************************************************************************/

int setIeeeStandardParameter(wifiApT *wifiApData, int stdMask)
{
    int8_t hwMode = (int8_t) (stdMask & 0x0F);
    int8_t numCheck = (int8_t)((hwMode & 0x1) + ((hwMode >> 1) & 0x1) +
                       ((hwMode >> 2) & 0x1) + ((hwMode >> 3) & 0x1));

    //Hardware mode should be set.
    if (numCheck == 0)
    {
        return -1;
    }
    //Hardware mode should be exclusive.
    if ( numCheck > 1 )
    {
        return -2;
    }

    if ( stdMask & WIFI_AP_BITMASK_IEEE_STD_AC )
    {
        // ieee80211ac=1 only works with hw_mode=a
        if ((stdMask & WIFI_AP_BITMASK_IEEE_STD_A) == 0)
        {
            return -3;
        }
    }

    if ( stdMask & WIFI_AP_BITMASK_IEEE_STD_H )
    {
        // ieee80211h=1 can be used only with ieee80211d=1
        if ((stdMask & WIFI_AP_BITMASK_IEEE_STD_D) == 0)
        {
            return -4;
        }
    }
    wifiApData->IeeeStdMask = stdMask;
    return 0;
}

/*******************************************************************************
 *                     set access point passphrase                             *
 * @return                                                                     *
 *     * -1 if passphrase is of invalid length                                 *
 *     *  0 if function succeeded                                              *
 ******************************************************************************/
int setPassPhraseParameter(wifiApT *wifiApData, const char  *passphrase)
{
    size_t length = strlen(passphrase);

    if ((length >= MIN_PASSPHRASE_LENGTH) &&
        (length <= MAX_PASSPHRASE_LENGTH))
    {
        // Store Passphrase to be used later during startup procedure
        memcpy(&wifiApData->passphrase[0], &passphrase[0], length);
        // Make sure there is a null termination
        wifiApData->passphrase[length] = '\0';
        return 0;
    }
    return -1;
}

/*******************************************************************************
 *                     set access point pre-shared key                         *
 * @return                                                                     *
 *     * -1 if preshared key is of invalid length                              *
 *     *  0 if function succeeded                                              *
 ******************************************************************************/
int setPreSharedKeyParameter(wifiApT *wifiApData, const char  *preSharedKey)
{
    uint32_t length = (uint32_t) strlen(preSharedKey);

    if (length <= MAX_PSK_LENGTH)
    {
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
    if (!strcasecmp(securityProtocol,"none")) {
        wifiApData->securityProtocol = WIFI_AP_SECURITY_NONE;
        return 0;
    }
    else if (!strcasecmp(securityProtocol,"WPA2")){
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
    uint32_t length = (uint32_t) strlen(countryCode);

    if (length == ISO_COUNTRYCODE_LENGTH)
    {
        memcpy(&wifiApData->countryCode[0], &countryCode[0], length );
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
    if ((maxNumberClients >= 1) && (maxNumberClients <= WIFI_AP_MAX_USERS))
    {
       wifiApData->maxNumberClient = maxNumberClients;
       return 0;
    }
    return -1;

}

/*******************************************************************************
 *     Set the access point IP address and client IP  addresses rang           *
 ******************************************************************************/
int setIpRangeParameters(wifiApT *wifiApData, const char *ip_ap, const char *ip_start, const char *ip_stop)
{
    struct sockaddr_in  saApPtr;
    struct sockaddr_in  saStartPtr;
    struct sockaddr_in  saStopPtr;
    const char         *parameterPtr = 0;

    // Check the parameters
    if ((ip_ap == NULL) || (ip_start == NULL) || (ip_stop == NULL))
    {
        goto OnErrorExit;
    }

    if ( (!strlen(ip_ap)) || (!strlen(ip_start)) || (!strlen(ip_stop)) )
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

    if (parameterPtr != NULL)
    {
        AFB_ERROR("Invalid %s IP address", parameterPtr);
        goto OnErrorExit;
    }
    else
    {
        unsigned int ap = ntohl(saApPtr.sin_addr.s_addr);
        unsigned int start = ntohl(saStartPtr.sin_addr.s_addr);
        unsigned int stop = ntohl(saStopPtr.sin_addr.s_addr);

        AFB_INFO("@AP=%x, @APstart=%x, @APstop=%x",
                ap, start, stop);

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

    size_t ipAddressNumElements;

    ipAddressNumElements = strlen(ip_ap);

    if ((0 < ipAddressNumElements) && (ipAddressNumElements <= MAX_IP_ADDRESS_LENGTH))
    {
        // Store ip address of AP to be used later during cleanup procedure
        utf8_Copy(wifiApData->ip_ap, ip_ap, sizeof(wifiApData->ip_ap), NULL);

        // Store AP range start ip address to be used later during cleanup procedure
        utf8_Copy(wifiApData->ip_start, ip_start, sizeof(wifiApData->ip_start), NULL);

        // Store AP range stop ip address to be used later during cleanup procedure
        utf8_Copy(wifiApData->ip_stop, ip_stop, sizeof(wifiApData->ip_stop), NULL);
    }
    else
    {
        goto OnErrorExit;
    }

    /* // start the dnsmasq service with the AP provided parameters
    if (setDnsmasqService(wifiApData, ip_ap, ip_start, ip_stop) < 0)
    {
        goto OnErrorExit;
    } */
    return 0;
OnErrorExit:
    return -1;
}