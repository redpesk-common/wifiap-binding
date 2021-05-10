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
#include "../lib/wifi-ap-utilities/wifi-ap-utilities.h"
#include "../lib/wifi-ap-utilities/wifi-ap-data.h"
#include "../lib/wifi-ap-utilities/wifi-ap-config.h"
#include "../lib/wifi-ap-utilities/wifi-ap-thread.h"

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
#define COMMAND_DNSMASQ_RESTART      " DNSMASQ_RESTART"

static struct event *events = NULL;
//static char scriptPath[4096] = "";

/***********************************************************************************************************************
 *                  The handle of the input pipe used to be notified of the WiFi events                                *
 ***********************************************************************************************************************/
static FILE *IwThreadPipePtr  = NULL;
thread_Obj_t *wifiApThreadPtr = NULL;

//----------------------------------------------------------------------------------------------------------------------

static struct cds_list_head wifiApList;

//----------------------------------------------------------------------------------------------------------------------

/*******************************************************************************
 *               Search for a specific event                                   *
 ******************************************************************************/
static struct event *event_get(const char *name)
{
    struct event *e = events;
    while(e && strcmp(e->name, name))
    {
        e = e->next;
    }
    return e;
}

/*******************************************************************************
 *                    Function to push event                                   *
 ******************************************************************************/
static int do_event_push(struct json_object *args, const char *name)
{
    struct event *e;
    e = event_get(name);
    return e ? afb_event_push(e->event, json_object_get(args)) : -1;
}


/*******************************************************************************
 *                 Function to create event of the name                        *
 ******************************************************************************/
static int event_add(const char *name)
{
    struct event *e;

    /* check valid name */
    e = event_get(name);
    if (e) return -1;

    /* creation */
    e = malloc(strlen(name) + sizeof *e + 1);
    if (!e) return -1;
    strcpy(e->name, name);

    /* make the event */
    e->event = afb_daemon_make_event(name);
    if (!e->event) { free(e); return -1; }

    /* link */
    e->next = events;
    events = e;
    return 0;
}


/*******************************************************************************
 *                 Function to subscribe event of the name                     *
 ******************************************************************************/
static int event_subscribe(afb_req_t request, const char *name)
{
    struct event *e;
    e = event_get(name);
    return e ? afb_req_subscribe(request, e->event) : -1;
}

/*******************************************************************************
 *                Function to unsubscribe event of the name                    *
 ******************************************************************************/
static int event_unsubscribe(afb_req_t request, const char *name)
{
    struct event *e;
    e = event_get(name);
    return e ? afb_req_unsubscribe(request, e->event) : -1;
}

/*******************************************************************************
 *                Subscribes for the event of name                             *
 ******************************************************************************/
static void subscribe(afb_req_t request)
{
    json_object *nameJ = afb_req_json(request);
    if (!nameJ){
        afb_req_fail(request, "invalid-syntax", "Missing parameter");
        return;
    }


    const char * name = json_object_get_string(nameJ);

    if (name == NULL)
        afb_req_fail(request, "failed", "bad arguments");
    else if (0 != event_subscribe(request, name))
        afb_req_fail(request, "failed", "subscription error");
    else
        afb_req_success(request, NULL, NULL);
}

/*******************************************************************************
 *                 Unsubscribes of the event of name                           *
 ******************************************************************************/
static void unsubscribe(afb_req_t request)
{
    json_object *nameJ = afb_req_json(request);
    if (!nameJ){
        afb_req_fail(request, "invalid-syntax", "Missing parameter");
        return;
    }


    const char * name = json_object_get_string(nameJ);

    if (name == NULL)
        afb_req_fail(request, "failed", "bad arguments");
    else if (0 != event_unsubscribe(request, name))
        afb_req_fail(request, "failed", "unsubscription error");
    else
        afb_req_success(request, NULL, NULL);
}

/***********************************************************************************************************************
 *                                          WiFi Client Thread Function                                                *
 **********************************************************************************************************************/
static void *WifiApThreadMainFunc(void *contextPtr)
{
    char path[PATH_MAX];
    char *ret;
    char *pathReentrant;
    int numberOfClientsConnected = 0;
    char eventInfo[WIFI_MAX_EVENT_INFO_LENGTH];

    AFB_INFO("wifiAp event report thread started on interface %s !", (char * ) contextPtr);
    char cmd[PATH_MAX];
    snprintf((char *)&cmd, sizeof(cmd),"iw event");

    AFB_INFO(" %s !", cmd);
    IwThreadPipePtr = popen(cmd, "r");

    if (NULL == IwThreadPipePtr)
    {
        AFB_ERROR("Failed to run command:\"iw event\" errno:%d %s",
                errno,
                strerror(errno));
        return NULL;
    }

    // Read the output one line at a time - output it.
    while (NULL != fgets(path, sizeof(path) - 1, IwThreadPipePtr))
    {
        AFB_DEBUG("PARSING:%s: len:%d", path, (int) strnlen(path, sizeof(path) - 1));
        if (NULL != (ret = strstr(path, "del station")))
        {
            pathReentrant = path;
            ret = strtok_r(pathReentrant, ":", &pathReentrant);
            if (NULL == ret)
            {
                AFB_WARNING("Failed to retrieve WLAN interface");
            }
            else
            {
                if(NULL !=  strstr(ret, (char*)contextPtr))
                {
                    numberOfClientsConnected--;
                    memcpy(eventInfo, "wifi client disconnected", 22);
                    eventInfo[22] = '\0';

                    json_object *eventResponseJ ;
                    wrap_json_pack(&eventResponseJ, "{ss,si}"
                                , "Event", eventInfo
                                , "number-client", numberOfClientsConnected
                                );

                    do_event_push(eventResponseJ,"wifiAp/client-state");
                }
            }
        }
        else if (NULL != (ret = strstr(path, "new station")))
        {
            pathReentrant = path;
            ret = strtok_r(pathReentrant, ":", &pathReentrant);
            if (NULL == ret)
            {
                AFB_WARNING("Failed to retrieve WLAN interface");
            }
            else
            {
                if(NULL !=  strstr(ret, (char*)contextPtr))
                {
                    numberOfClientsConnected++;
                    memcpy(eventInfo, "wifi client connected", 22);
                    eventInfo[22] = '\0';

                    json_object *eventResponseJ ;
                    wrap_json_pack(&eventResponseJ, "{ss,si}"
                                , "Event", eventInfo
                                , "number-client", numberOfClientsConnected
                                );

                    do_event_push(eventResponseJ,"wifiAp/client-state");
                }
            }
        }
    }

    return NULL;
}

/***********************************************************************************************************************
 *                                          Thread destructor Function                                                 *
 **********************************************************************************************************************/
static void threadDestructorFunc(void *contextPtr)
{
    int systemResult;

    char cmd[PATH_MAX];
    snprintf((char *)&cmd, sizeof(cmd), "%s %s ", (char*) contextPtr, COMMAND_WIFI_UNSET_EVENT);

    // Kill the script launched by popen() in Client thread
    systemResult = system(cmd);

    if ((!WIFEXITED(systemResult)) || (0 != WEXITSTATUS(systemResult)))
    {
        AFB_WARNING("Unable to kill the WIFI events script %d", WEXITSTATUS(systemResult));
    }

    if (IwThreadPipePtr)
    {
        // And close FP used in created thread
        pclose(IwThreadPipePtr);
        IwThreadPipePtr = NULL;
    }
    return ;
}

/*******************************************************************************
 *                      start access point function                            *
 ******************************************************************************/
int startAp(wifiApT *wifiApData)
{
    int     systemResult;
    AFB_INFO("Starting AP ...");

    // Check that an SSID is provided before starting
    if ('\0' == wifiApData->ssid[0])
    {
        AFB_ERROR("Unable to start AP because no valid SSID provided");
        return -1;
    }

    // Check channel number is properly set before starting
    if ((wifiApData->channelNumber < wifiApData->channel.MIN_CHANNEL_VALUE) ||
            (wifiApData->channelNumber > wifiApData->channel.MAX_CHANNEL_VALUE))
    {
        AFB_ERROR("Unable to start AP because no valid channel number provided");
        return -2;
    }

    // Create hostapd.conf file in /tmp
    if (GenerateHostApConfFile(wifiApData) != 0)
    {
        AFB_ERROR("Failed to generate hostapd.conf");
        return -3;
    }
    else AFB_INFO("AP configuration file has been generated");

    char cmd[PATH_MAX];
    snprintf((char *)&cmd, sizeof(cmd), " %s %s %s",
                wifiApData->wifiScriptPath,
                COMMAND_WIFI_HW_START,wifiApData->interfaceName);

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
        return -4;
    }
    // Return value of 100 means WiFi card may not work.
    else if ( WEXITSTATUS(systemResult) == 100)
    {
        AFB_ERROR("Unable to reset WiFi card");
        return -5;
    }
    // WiFi card failed to start.
    else
    {
        AFB_WARNING("Failed to start WiFi AP command \"%s\" systemResult (%d)",
                COMMAND_WIFI_HW_START, systemResult);
        return -6;
    }

    AFB_INFO("Started WiFi AP command \"%s\" successfully",
                COMMAND_WIFI_HW_START);

    // Start Access Point cmd: /bin/hostapd /etc/hostapd.conf
    snprintf((char *)&cmd, sizeof(cmd), " %s %s %s",
                wifiApData->wifiScriptPath,
                COMMAND_WIFIAP_HOSTAPD_START, wifiApData->interfaceName);

    systemResult = system(cmd);
    if ((!WIFEXITED(systemResult)) || (0 != WEXITSTATUS(systemResult)))
    {
        AFB_ERROR("WiFi Client Command \"%s\" Failed: (%d)",
                COMMAND_WIFIAP_HOSTAPD_START,
                systemResult);
        // Remove generated hostapd.conf file
        remove(WIFI_HOSTAPD_FILE);
        return -6;
    }

    //create wifi-ap event thread
    wifiApThreadPtr = CreateThread("WifiApThread",WifiApThreadMainFunc, wifiApData->interfaceName);
    if(!wifiApThreadPtr) AFB_ERROR("Unable to create thread!");

    //set thread to joinable
    int error = setThreadJoinable(wifiApThreadPtr->threadId);
    if(error) AFB_ERROR("Unable to set wifiAp thread as joinable!");

    //add thread destructor
    error = addDestructorToThread(wifiApThreadPtr->threadId, threadDestructorFunc, wifiApData->wifiScriptPath);
    if(error) AFB_ERROR("Unable to add a destructor to the wifiAp thread!");

    //start thread
    error = startThread(wifiApThreadPtr->threadId);
    if(error) AFB_ERROR("Unable to start wifiAp thread!");

    AFB_INFO("WiFi AP started correclty");
    return 0;
}

/*******************************************************************************
 *                 start access point verb function                            *
 ******************************************************************************/
static void start(afb_req_t req)
{
    AFB_INFO("WiFi access point start verb function");

    afb_api_t wifiAP = afb_req_get_api(req);
    wifiApT *wifiApData = (wifiApT*) afb_api_get_userdata(wifiAP);
    if (!wifiApData)
    {
        afb_req_fail(req, "wifiAp_data", "Can't get wifi access point data");
        return;
    }

    int error = startAp(wifiApData);

    if(!error)
    {
        AFB_INFO("WiFi AP started correclty");
        afb_req_success(req, NULL, "Access point started successfully");
        return;
    }

    else if (error == -1)
    {
        afb_req_fail(req, "failed - Bad parameter", "No valid SSID provided");
        return;
    }
    else if (error == -2)
    {
        afb_req_fail(req, "failed - Bad parameter", "No valid channel number provided");
        return;
    }
    else if (error == -3)
    {
        afb_req_fail(req, "failed", "Failed to generate hostapd.conf");
        return;
    }
    else if (error == -7) goto error;
    else if (error < 0)
    {
        afb_req_fail(req, "failed", "WiFi client command WIFI_START failed");
        return;
    }
error:
    afb_req_fail(req, "failed", "Unspecified internal error\n");
    return;
}
/*******************************************************************************
 *               stop access point verb function                               *
 ******************************************************************************/

static void stop(afb_req_t req){

    int status;

    afb_api_t wifiAP = afb_req_get_api(req);

    wifiApT *wifiApData = (wifiApT*) afb_api_get_userdata(wifiAP);
    if (!wifiApData)
    {
        afb_req_fail(req, "wifiAp_data", "Can't get wifi access point data");
        return;
    }

    char cmd[PATH_MAX];

    snprintf((char *)&cmd, sizeof(cmd), "%s %s %s",
                wifiApData->wifiScriptPath,
                COMMAND_WIFIAP_HOSTAPD_STOP, wifiApData->interfaceName);

    status = system(cmd);
    if ((!WIFEXITED(status)) || (0 != WEXITSTATUS(status)))
    {
        AFB_ERROR("WiFi AP Command \"%s\" Failed: (%d)",
                COMMAND_WIFIAP_HOSTAPD_STOP,
                status);
        goto onErrorExit;
    }

    snprintf((char *)&cmd, sizeof(cmd), "%s %s %s",
                wifiApData->wifiScriptPath,
                COMMAND_WIFI_HW_STOP, wifiApData->interfaceName);

    status = system(cmd);
    if ((!WIFEXITED(status)) || (0 != WEXITSTATUS(status)))
    {
        AFB_ERROR("WiFi AP Command \"%s\" Failed: (%d)", COMMAND_WIFI_HW_STOP, status);
        goto onErrorExit;
    }

    /* Terminate the created thread */
    if (0 != cancelThread(wifiApThreadPtr->threadId)){
        afb_req_fail(req, "failed", "No event thread found\n");
        return;
    }
    if (0 != JoinThread(wifiApThreadPtr->threadId, NULL))
    {
        goto onErrorExit;
    }

    afb_req_success(req, NULL, "Access Point was stoped successfully");
    return;

onErrorExit:
    afb_req_fail(req, "failed", "Unspecified internal error\n");
    return;
}

/*******************************************************************************
 *               set the wifi access point SSID                                *
 ******************************************************************************/
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


    AFB_INFO("Set SSID");

    if (setSsidParameter(wifiApData,ssidPtr))
    {
        AFB_INFO("SSID was set successfully %s", wifiApData->ssid);
        json_object_object_add(responseJ,"SSID", json_object_new_string(wifiApData->ssid));
        afb_req_success(req, responseJ, "SSID set successfully");
    }
    else
    {
        afb_req_fail_f(req, "failed - Bad parameter", "Wi-Fi - SSID length exceeds (MAX_SSID_LENGTH = %d)!", MAX_SSID_LENGTH);
    }
    return;
}

/*******************************************************************************
 *                     set access point passphrase                             *
 ******************************************************************************/

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
        if (setPassPhraseParameter(wifiApData, passphrase) == 0)
        {
            AFB_INFO("Passphrase was set successfully");
            json_object_object_add(responseJ,"Passphrase", json_object_new_string(wifiApData->passphrase));
            afb_req_success(req, responseJ, "Passphrase set successfully!");
            return;
        }
        afb_req_fail(req, "failed - Bad parameter", "Wi-Fi - PassPhrase with Invalid length ");
        return;
    }
    afb_req_fail(req, "invalid-syntax", "Missing parameter");
    return;

}

/*******************************************************************************
 *           set if access point announce its presence or not                  *
 ******************************************************************************/

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

/*******************************************************************************
 *           set the IEEE standard to use for the access point                 *
 ******************************************************************************/

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

    AFB_INFO("Set IeeeStdBitMask : 0x%X", stdMask);
    //Hardware mode should be set.
    if (setIeeeStandardParameter(wifiApData,stdMask) == -1)
    {
        AFB_WARNING("No hardware mode is set");
        goto onErrorExit;
    }
    //Hardware mode should be exclusive.
    if ( setIeeeStandardParameter(wifiApData,stdMask) == -2 )
    {
        AFB_WARNING("Only one hardware mode can be set");
        goto onErrorExit;
    }

    if ( setIeeeStandardParameter(wifiApData,stdMask) == -3 )
    {
        AFB_WARNING("ieee80211ac=1 only works with hw_mode=a");
        goto onErrorExit;
    }

    if ( setIeeeStandardParameter(wifiApData,stdMask) == -4 )
    {
        AFB_WARNING("ieee80211h=1 only works with ieee80211d=1");
        goto onErrorExit;
    }
    else if (!setIeeeStandardParameter(wifiApData,stdMask))
    {
        AFB_INFO("IeeeStdBitMask was set successfully");

        json_object_object_add(responseJ,"stdMask", json_object_new_int(wifiApData->IeeeStdMask));
        afb_req_success(req, responseJ, "stdMask is set successfully");
        return;
    }
onErrorExit:
    afb_req_fail(req, "Failed", "Parameter is invalid!");
    return;
}

/*******************************************************************************
 *           get the IEEE standard used for the access point                   *
 ******************************************************************************/

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

/*******************************************************************************
 *               set the number of wifi access point channel                   *
 ******************************************************************************/
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

    if (setChannelParameter(wifiApData, channelNumber))
    {
        AFB_INFO("Channel number was set successfully to %d", wifiApData->channelNumber);
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

/*******************************************************************************
 *                     set access point security protocol                      *
 ******************************************************************************/

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

    if (setSecurityProtocolParameter(wifiApData, securityProtocol) == 0) {
        afb_req_success(req,NULL,"Security parameter was set to none!");
        return;
    }
    else if (setSecurityProtocolParameter(wifiApData, securityProtocol) == 1){
        afb_req_success(req,NULL,"Security parameter was set to WPA2!");
        return;
    }
    else
    {
        afb_req_fail(req, "Bad-Parameter", "Parameter is invalid!");
        return;
    }
}

/*******************************************************************************
 *                     set access point pre-shared key                         *
 ******************************************************************************/
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
        if (setPreSharedKeyParameter(wifiApData, preSharedKey) == 0)
        {
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

/*******************************************************************************
 *               set the country code to use for access point                  *
 ******************************************************************************/

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
        if (setCountryCodeParameter(wifiApData, countryCode) == 0)
        {
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
    return;

}

/*******************************************************************************
 *               set the max number of clients of access point                 *
 ******************************************************************************/

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

    if (setMaxNumberClients(wifiApData, maxNumberClients) == 0)
    {
       AFB_NOTICE("The maximum number of clients was set to %d",wifiApData->maxNumberClient);
       json_object_object_add(responseJ,"maxNumberClients", json_object_new_int(wifiApData->maxNumberClient));
       afb_req_success(req,responseJ,"Max Number of clients was set successfully!");
       return;
    }
    else afb_req_fail(req, "Bad-Parameter", "The value is out of range");
    return;

}

/*******************************************************************************
 *                Start the access point dnsmasq service                       *
 ******************************************************************************/
static int setDnsmasqService(wifiApT *wifiApData, const char *ip_ap, const char *ip_start, const char *ip_stop, const char *ip_netmask)
{
    struct sockaddr_in  saApPtr;
    struct sockaddr_in  saStartPtr;
    struct sockaddr_in  saStopPtr;
    struct sockaddr_in  saNetmaskPtr;
    //struct sockaddr_in  saSubnetPtr;
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

    if (parameterPtr != NULL)
    {
        AFB_ERROR("Invalid %s IP address", parameterPtr);
        return -1;
    }
    else
    {
        unsigned int ap = ntohl(saApPtr.sin_addr.s_addr);
        unsigned int start = ntohl(saStartPtr.sin_addr.s_addr);
        unsigned int stop = ntohl(saStopPtr.sin_addr.s_addr);
        unsigned int netmask = ntohl(saNetmaskPtr.sin_addr.s_addr);
        //unsigned int subnet = ntohl(saSubnetPtr.sin_addr.s_addr);

        AFB_INFO("@AP=%x, @APstart=%x, @APstop=%x, @APnetmask=%x  @AP_CIDR=%s",
                ap, start, stop, netmask, ip_ap_cidr);

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

        snprintf((char *)&cmd, sizeof(cmd), " %s %s %s %s",
                wifiApData->wifiScriptPath,
                COMMAND_WIFIAP_WLAN_UP,
                wifiApData->interfaceName,
                ip_ap);

        systemResult = system(cmd);
        if ( WEXITSTATUS (systemResult) != 0)
        {
            AFB_ERROR("Unable to mount the network interface");
            goto OnErrorExit;
        }
        else
        {

            int error = createDnsmasqConfigFile(ip_ap, ip_start, ip_stop);
            if (error) {
                AFB_ERROR("Unable to create DHCP config file");
                goto OnErrorExit;
            }

            AFB_INFO("@AP=%s, @APstart=%s, @APstop=%s", ip_ap, ip_start, ip_stop);

            /* // Insert the rule allowing the DHCP ports on WLAN
            snprintf((char *)&cmd, sizeof(cmd), " %s %s %s",
                wifiApData->wifiScriptPath,
                COMMAND_IPTABLE_DHCP_INSERT, wifiApData->interfaceName);

            systemResult = system(cmd);

            if (WEXITSTATUS (systemResult) != 0)
            {
                AFB_ERROR("Unable to allow DHCP ports.");
                goto OnErrorExit;
            } */

            char cmd[PATH_MAX];
            snprintf((char *)&cmd, sizeof(cmd), "%s %s %s %s",
                    wifiApData->wifiScriptPath,
                    COMMAND_DNSMASQ_RESTART,
                    wifiApData->interfaceName,
                    ip_ap_cidr);

            systemResult = system(cmd);
            if (WEXITSTATUS (systemResult) != 0)
            {
                AFB_ERROR("Unable to restart the DHCP server.");
                goto OnErrorExit;
            }
        }
    }
    return 0;
OnErrorExit:
    return -2;
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

    if (setIpRangeParameters(wifiApData, ip_ap, ip_start, ip_stop))
    {
        afb_req_fail_f(req, "Failed", "Unable to set IP addresses for the Access point");
        return;
    }

    error = setDnsmasqService(wifiApData, ip_ap, ip_start, ip_stop, ip_netmask);
    if(error)
    {
        afb_req_fail_f(req, "Failed", "error %d caught", error);
        return;
    }
    afb_req_success(req,NULL,"IP range was set successfully!");
    return;
}
/*******************************************************************************
 *               Initialize the wifi data structure                            *
 ******************************************************************************/

int wifiApConfig(afb_api_t apiHandle, CtlSectionT *section, json_object *wifiApConfigJ)
{

    char *uid, *ssid , *securityProtocol ,*passphrase ,*countryCode;
    const char *ip_ap, *ip_start, *ip_stop, *ip_netmask;
    bool start;

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

    int error = wrap_json_unpack(wifiApConfigJ, "{s?s,s?b,ss,s?s,s?i,s?b,si,ss,s?s,s?s,s?i,s?s,s?s,s?s, s?s}"
            , "uid"              , &uid
            , "startAtInit"      , &start
            , "interfaceName"    , &wifiApData->interfaceName
            , "ssid"             , &ssid
            , "channelNumber"    , &wifiApData->channelNumber
            , "discoverable"     , &wifiApData->discoverable
            , "IeeeStdMask"      , &wifiApData->IeeeStdMask
            , "securityProtocol" , &securityProtocol
            , "passphrase"       , &passphrase
            , "countryCode"      , &countryCode
            , "maxNumberClient"  , &wifiApData->maxNumberClient
            , "ip_ap"            , &ip_ap
            , "ip_start"         , &ip_start
            , "ip_stop"          , &ip_stop
            , "ip_netmask"       , &ip_netmask
            );
    if (error) {
		AFB_API_ERROR(apiHandle, "%s: invalid-syntax error=%s args=%s",
				__func__, wrap_json_get_error_string(error), json_object_get_string(wifiApConfigJ));
        return -3;
    }

    //set default MIN and MAX channel values

    wifiApData->channel.MIN_CHANNEL_VALUE = MIN_CHANNEL_VALUE_DEF;
    wifiApData->channel.MAX_CHANNEL_VALUE = MAX_CHANNEL_VALUE_DEF;

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
    if(start)
    {
        if (setIpRangeParameters(wifiApData,ip_ap, ip_start, ip_stop) <0) return -9;

        if(setDnsmasqService(wifiApData, ip_ap, ip_start, ip_stop, ip_netmask) == 0)
        {
            error = startAp(wifiApData) ;
            if(error)
            {
                return -10;
            }
            AFB_INFO("WiFi AP started correclty");
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
    { .verb = "subscribe"           , .callback = subscribe          , .info = "Subscribe to wifi-ap events"},
    { .verb = "unsubscribe"         , .callback = unsubscribe        , .info = "Unsubscribe to wifi-ap events"},
    { .verb = "SetMaxNumberClients" , .callback = SetMaxNumberClients,  .info = "Set the maximum number of clients allowed to be connected to WiFiAP at the same time"}
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

    event_add("wifiAp/client-state");

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