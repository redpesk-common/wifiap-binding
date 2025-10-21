/*
 * Copyright (C) 2016-2024 "IoT.bzh"
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

#include <stdio.h>
#include <string.h>

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <json-c/json.h>

#include <rp-utils/rp-jsonc.h>

#define AFB_BINDING_VERSION 4
#include <afb/afb-binding.h>
#include <afb-helpers4/afb-req-utils.h>

#include "../lib/wifi-ap-utilities/wifi-ap-config.h"
#include "../lib/wifi-ap-utilities/wifi-ap-data.h"
#include "../lib/wifi-ap-utilities/wifi-ap-thread.h"
#include "../lib/wifi-ap-utilities/wifi-ap-utilities.h"


// Set of commands to drive the WiFi features.
#define COMMAND_WIFI_HW_START        " WIFI_START"
#define COMMAND_WIFI_HW_STOP         " WIFI_STOP"
#define COMMAND_WIFI_SET_EVENT       " WIFI_SET_EVENT"
#define COMMAND_WIFI_UNSET_EVENT     " WIFI_UNSET_EVENT"
#define COMMAND_WIFI_FIREWALLD_ALLOW " WIFI_FIREWALLD_ALLOW"
#define COMMAND_WIFI_NM_UNMANAGE     " WIFI_NM_UNMANAGE"
#define COMMAND_WIFIAP_HOSTAPD_START " WIFIAP_HOSTAPD_START"
#define COMMAND_WIFIAP_HOSTAPD_STOP  " WIFIAP_HOSTAPD_STOP"
#define COMMAND_WIFIAP_WLAN_UP       " WIFIAP_WLAN_UP"
#define COMMAND_DNSMASQ_RESTART      " DNSMASQ_RESTART"

#ifdef TEST_MODE
#define COMMAND_GET_VIRTUAL_INTERFACE_NAME "GET_VIRTUAL_INTERFACE_NAME"
#endif

#define MAX_IP_ADDRESS_LENGTH      15
#define WIFI_MAX_EVENT_INFO_LENGTH 512

#define HARDWARE_MODE_MASK 0x000F  // Hardware mode mask
#define PATH_MAX           8192

// path to Wifi platform adapter shell script
#ifdef TEST_MODE
#define WIFI_SCRIPT      APP_DIR_ "/var/wifi_setup_test.sh"
#define PATH_CONFIG_FILE APP_DIR_ "/etc/wifiap-config.json"
#else
#define WIFI_SCRIPT      APP_DIR_ "/var/wifi_setup.sh"
#define PATH_CONFIG_FILE APP_DIR_ "/etc/wifiap-config.json"
#endif

struct event
{
    struct event *next;
    afb_event_t event;
    char name[];
};

static struct event *events = NULL;

static pthread_mutex_t status_mutex = PTHREAD_MUTEX_INITIALIZER;

/****************************************************************************************
 *                  The handle of the input pipe used to be notified of the WiFi events *
 ***************************************************************************************/
static FILE *IwThreadPipePtr = NULL;
thread_Obj_t *wifiApThreadPtr = NULL;

//---------------------------------------------------------------------------------------

static struct cds_list_head wifiApList;

//---------------------------------------------------------------------------------------

/*******************************************************************************
 *               Search for a specific event                                   *
 ******************************************************************************/
static struct event *event_get(const char *name)
{
    struct event *e = events;
    while (e && strcmp(e->name, name)) {
        e = e->next;
    }
    return e;
}

/*******************************************************************************
 *                    Function to push event                                   *
 ******************************************************************************/
static int do_event_push(struct json_object *args, const char *name)
{
    int err;
    afb_data_t event_data;
    struct event *e = event_get(name);

    if (!e)
        return -1;

    err = afb_create_data_copy(&event_data, AFB_PREDEFINED_TYPE_JSON_C, args, 0);
    if (err < 0)
        return err;

    return afb_event_push(e->event, 1, &event_data);
}

/*******************************************************************************
 *                 Function to create event of the name                        *
 ******************************************************************************/
static int event_add(afb_api_t api, const char *name)
{
    struct event *e;

    /* check valid name */
    e = event_get(name);
    if (e)
        return -1;

    /* creation of the event name */
    e = malloc(strlen(name) + sizeof *e + 1);
    if (!e)
        return -1;
    strcpy(e->name, name);

    /* make the event */
    afb_api_new_event(api, name, &(e->event));
    if (!e->event) {
        free(e);
        return -1;
    }

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
static void subscribe(afb_req_t request, unsigned nparams, afb_data_t const *params)
{
    json_object *nameJ = (json_object *)afb_data_ro_pointer(params[0]);

    if (!nameJ) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Missing parameter");
        return;
    }

    const char *name = json_object_get_string(nameJ);

    if (name == NULL)
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Bad arguments");
    else if (0 != event_subscribe(request, name))
        afb_req_reply_string(request, AFB_ERRNO_INTERNAL_ERROR, "Subscription error");
    else
        afb_req_reply(request, 0, 0, NULL);
}

/*******************************************************************************
 *                 Unsubscribes of the event of name                           *
 ******************************************************************************/
static void unsubscribe(afb_req_t request, unsigned nparams, afb_data_t const *params)
{
    json_object *nameJ = (json_object *)afb_data_ro_pointer(params[0]);

    if (!nameJ) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Missing parameter");
        return;
    }

    const char *name = json_object_get_string(nameJ);

    if (name == NULL)
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Bad arguments");
    else if (0 != event_unsubscribe(request, name))
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Subscription error");
    else
        afb_req_reply(request, 0, 0, NULL);
}

/*****************************************************************************************
 *                                                           WiFi Client Thread Function *
 *****************************************************************************************/
static void *WifiApThreadMainFunc(void *contextPtr)
{
    char path[PATH_MAX];
    char *ret;
    char *pathReentrant;
    int numberOfClientsConnected = 0;
    char eventInfo[WIFI_MAX_EVENT_INFO_LENGTH];

    AFB_INFO("wifiAp event report thread started on interface %s !", (char *)contextPtr);
    char cmd[PATH_MAX];
    snprintf((char *)&cmd, sizeof(cmd), "iw event");

    AFB_INFO(" %s !", cmd);
    IwThreadPipePtr = popen(cmd, "r");

    if (NULL == IwThreadPipePtr) {
        AFB_ERROR("Failed to run command:\"iw event\" errno:%d %s", errno, strerror(errno));
        return NULL;
    }

    // Read the output one line at a time - output it.
    while (NULL != fgets(path, sizeof(path) - 1, IwThreadPipePtr)) {
        AFB_DEBUG("PARSING:%s: len:%d", path, (int)strnlen(path, sizeof(path) - 1));
        if (NULL != (ret = strstr(path, "del station"))) {
            pathReentrant = path;
            ret = strtok_r(pathReentrant, ":", &pathReentrant);
            if (NULL == ret) {
                AFB_WARNING("Failed to retrieve WLAN interface");
            }
            else {
                if (NULL != strstr(ret, (char *)contextPtr)) {
                    numberOfClientsConnected--;
                    memcpy(eventInfo, "WiFi client disconnected", 22);
                    eventInfo[22] = '\0';

                    json_object *eventResponseJ;
                    rp_jsonc_pack(&eventResponseJ, "{ss,si}", "Event", eventInfo, "number-client",
                                  numberOfClientsConnected);

                    do_event_push(eventResponseJ, "client-state");
                }
            }
        }
        else if (NULL != (ret = strstr(path, "new station"))) {
            pathReentrant = path;
            ret = strtok_r(pathReentrant, ":", &pathReentrant);
            if (NULL == ret) {
                AFB_WARNING("Failed to retrieve WLAN interface");
            }
            else {
                if (NULL != strstr(ret, (char *)contextPtr)) {
                    numberOfClientsConnected++;
                    memcpy(eventInfo, "WiFi client connected", 22);
                    eventInfo[22] = '\0';

                    json_object *eventResponseJ;
                    rp_jsonc_pack(&eventResponseJ, "{ss,si}", "Event", eventInfo, "number-client",
                                  numberOfClientsConnected);

                    do_event_push(eventResponseJ, "client-state");
                }
            }
        }
    }

    return NULL;
}

/*****************************************************************************************
 *                                                            Thread destructor Function *
 ****************************************************************************************/
static void threadDestructorFunc(void *contextPtr)
{
    int systemResult;

    char cmd[PATH_MAX];
    snprintf((char *)&cmd, sizeof(cmd), "%s %s ", (char *)contextPtr, COMMAND_WIFI_UNSET_EVENT);

    // Kill the script launched by popen() in Client thread
    systemResult = system(cmd);

    if ((!WIFEXITED(systemResult)) || (0 != WEXITSTATUS(systemResult))) {
        AFB_WARNING("Unable to kill the WIFI events script %d", WEXITSTATUS(systemResult));
    }

    if (IwThreadPipePtr) {
        // And close FP used in created thread
        pclose(IwThreadPipePtr);
        IwThreadPipePtr = NULL;
    }
    return;
}

/*******************************************************************************
 *             Check and resolve conflict with NM if exists                    *
 ******************************************************************************/
static void check_and_resolve_conflicts_with_NM(wifiApT *wifiApData)
{
    AFB_INFO("Check if Network Manager is installed");

    int systemResult = 0;

    char cmd[PATH_MAX];

    strncpy(wifiApData->wifiScriptPath, WIFI_SCRIPT, sizeof(wifiApData->wifiScriptPath) - 1);

    snprintf((char *)&cmd, sizeof(cmd), " %s %s %s", wifiApData->wifiScriptPath,
             COMMAND_WIFI_NM_UNMANAGE, wifiApData->interfaceName);

    // Check if nmcli installed
    int ret = system("nmcli -v >/dev/null");
    if (ret == 0) {
        AFB_DEBUG("Network Manager is installed on system!");

        // Add polkit rules to allow nmcli command to be run by service
        createPolkitRulesFile_NM();

        // Disable Network Manager for interface
        AFB_WARNING("interface %s WILL no longer be managed by Network Manager...",
                    wifiApData->interfaceName);
        systemResult = system(cmd);
        if (systemResult == 0)
            AFB_DEBUG("Network Manager IS disabled for interface %s!", wifiApData->interfaceName);
        else
            AFB_ERROR("Unable to disable Network Manager for interface %s!",
                      wifiApData->interfaceName);
    }
}

/*******************************************************************************
 *          Allow DHCP traffic through if firewalld is running                 *
 ******************************************************************************/
static void check_if_firewalld_running_and_allow_dhcp_traffic(wifiApT *wifiApData)
{
    AFB_INFO("Check if firewalld service is enabled");

    int systemResult = 0;

    char cmd[PATH_MAX];

    strncpy(wifiApData->wifiScriptPath, WIFI_SCRIPT, sizeof(wifiApData->wifiScriptPath) - 1);

    snprintf((char *)&cmd, sizeof(cmd), " %s %s %s", wifiApData->wifiScriptPath,
             COMMAND_WIFI_FIREWALLD_ALLOW, wifiApData->interfaceName);

    int ret = system("pgrep firewalld >/dev/null");
    if (ret == 0) {
        AFB_DEBUG("Firewalld is enabled on target!");
        createPolkitRulesFile_Firewalld();

        // Allow DHCP traffic through
        AFB_WARNING("DHCP traffic WILL be no longer be blocked by firewalld...");
        systemResult = system(cmd);
        if (systemResult == 0)
            AFB_DEBUG("DHCP traffic IS allowed through!");
        else
            AFB_ERROR("Unable to allow DHCP traffic through!");
    }
}

/*******************************************************************************
 *                Start the access point dnsmasq service                       *
 ******************************************************************************/
static int setDnsmasqService(wifiApT *wifiApData)
{
    struct sockaddr_in saApPtr;
    struct sockaddr_in saStartPtr;
    struct sockaddr_in saStopPtr;
    struct sockaddr_in saNetmaskPtr;
    const char *parameterPtr = 0;

    // Check the parameters
    if ((wifiApData->ip_ap[0] == '\0') || (wifiApData->ip_start[0] == '\0') ||
        (wifiApData->ip_stop[0] == '\0') || (wifiApData->ip_netmask[0] == '\0')) {
        goto OnErrorExit;
    }

    if ((!strlen(wifiApData->ip_ap)) || (!strlen(wifiApData->ip_start)) ||
        (!strlen(wifiApData->ip_stop)) || (!strlen(wifiApData->ip_netmask))) {
        goto OnErrorExit;
    }

    if (inet_pton(AF_INET, wifiApData->ip_ap, &(saApPtr.sin_addr)) <= 0) {
        parameterPtr = "AP";
    }
    else if (inet_pton(AF_INET, wifiApData->ip_start, &(saStartPtr.sin_addr)) <= 0) {
        parameterPtr = "start";
    }
    else if (inet_pton(AF_INET, wifiApData->ip_stop, &(saStopPtr.sin_addr)) <= 0) {
        parameterPtr = "stop";
    }
    else if (inet_pton(AF_INET, wifiApData->ip_netmask, &(saNetmaskPtr.sin_addr)) <= 0) {
        parameterPtr = "Netmask";
    }

    // get ip address with CIDR annotation
    int netmask_cidr = toCidr(wifiApData->ip_netmask);
    char ip_ap_cidr[128];
    snprintf((char *)&ip_ap_cidr, sizeof(ip_ap_cidr), "%s/%d", wifiApData->ip_ap, netmask_cidr);

    if (parameterPtr != NULL) {
        AFB_ERROR("Invalid %s IP address", parameterPtr);
        pthread_mutex_lock(&status_mutex);
        wifiApData->status = "failure";
        pthread_mutex_unlock(&status_mutex);
        return -1;
    }
    else {
        unsigned int ap = ntohl(saApPtr.sin_addr.s_addr);
        unsigned int start = ntohl(saStartPtr.sin_addr.s_addr);
        unsigned int stop = ntohl(saStopPtr.sin_addr.s_addr);
        unsigned int netmask = ntohl(saNetmaskPtr.sin_addr.s_addr);

        AFB_INFO("@AP=%x, @APstart=%x, @APstop=%x, @APnetmask=%x  @AP_CIDR=%s", ap, start, stop,
                 netmask, ip_ap_cidr);

        if (start > stop) {
            AFB_INFO("Need to swap start & stop IP addresses");
            start = start ^ stop;
            stop = stop ^ start;
            start = start ^ stop;
        }

        if ((ap >= start) && (ap <= stop)) {
            AFB_ERROR("AP IP address is within the range");
            goto OnErrorExit;
        }
    }

    {
        char cmd[PATH_MAX];
        int systemResult;

        strncpy(wifiApData->wifiScriptPath, WIFI_SCRIPT, sizeof(wifiApData->wifiScriptPath) - 1);

        snprintf((char *)&cmd, sizeof(cmd), " %s %s %s %s", wifiApData->wifiScriptPath,
                 COMMAND_WIFIAP_WLAN_UP, wifiApData->interfaceName, wifiApData->ip_ap);

        systemResult = system(cmd);
        if (WEXITSTATUS(systemResult) != 0) {
            AFB_ERROR("Unable to mount the network interface");
            goto OnErrorExit;
        }
        else {
            int error = createHostsConfigFile(wifiApData->ip_ap, wifiApData->hostName);
            if (error) {
                AFB_ERROR("Unable to add a new hostname config file");
                goto OnErrorExit;
            }

            error = createDnsmasqConfigFile(wifiApData->ip_ap, wifiApData->ip_start,
                                            wifiApData->ip_stop, wifiApData->domainName);
            if (error) {
                AFB_ERROR("Unable to create Dnsmasq config file");
                goto OnErrorExit;
            }

            AFB_INFO("@AP=%s, @APstart=%s, @APstop=%s", wifiApData->ip_ap, wifiApData->ip_start,
                     wifiApData->ip_stop);

            char cmd[PATH_MAX];

            strncpy(wifiApData->wifiScriptPath, WIFI_SCRIPT,
                    sizeof(wifiApData->wifiScriptPath) - 1);

            snprintf((char *)&cmd, sizeof(cmd), "%s %s %s %s", wifiApData->wifiScriptPath,
                     COMMAND_DNSMASQ_RESTART, wifiApData->interfaceName, ip_ap_cidr);

            systemResult = system(cmd);
            if (WEXITSTATUS(systemResult) != 0) {
                AFB_ERROR("Unable to restart the Dnsmasq.");
                goto OnErrorExit;
            }
        }
    }
    AFB_INFO("Dnsmasq configuration file created successfully!");
    return 0;
OnErrorExit:
    pthread_mutex_lock(&status_mutex);
    wifiApData->status = "failure";
    pthread_mutex_unlock(&status_mutex);

    return -2;
}

/*******************************************************************************
 *                      start access point function                            *
 ******************************************************************************/
int startAp(wifiApT *wifiApData)
{
    int systemResult;
    AFB_INFO("Starting AP ...");

    const char *DnsmasqConfigFileName = "/tmp/dnsmasq.wlan.conf";
    const char *HotsConfigFileName = "/tmp/add_hosts";
    if (checkFileExists(DnsmasqConfigFileName) || checkFileExists(HotsConfigFileName)) {
        AFB_WARNING("Need to clean previous configuration for AP!");
        char cmd[PATH_MAX];

        strncpy(wifiApData->wifiScriptPath, WIFI_SCRIPT, sizeof(wifiApData->wifiScriptPath) - 1);

        snprintf((char *)&cmd, sizeof(cmd), "%s %s %s", wifiApData->wifiScriptPath,
                 COMMAND_WIFIAP_HOSTAPD_STOP, wifiApData->interfaceName);

        systemResult = system(cmd);
        if ((!WIFEXITED(systemResult)) || (0 != WEXITSTATUS(systemResult))) {
            AFB_ERROR("WiFi AP Command \"%s\" Failed: (%d)", COMMAND_WIFIAP_HOSTAPD_STOP,
                      systemResult);
            pthread_mutex_lock(&status_mutex);
            wifiApData->status = "failure";
            pthread_mutex_unlock(&status_mutex);
            return -9;
        }
    }

    int error = setDnsmasqService(wifiApData);
    if (error) {
        AFB_ERROR("Failed to set up Dnsmasq (error: %d). Checking system...", error);

        pthread_mutex_lock(&status_mutex);
        wifiApData->status = "failure";
        pthread_mutex_unlock(&status_mutex);
        return -8;
    }

    // Check that an SSID is provided before starting
    if ('\0' == wifiApData->ssid[0]) {
        AFB_ERROR("Unable to start AP because no valid SSID provided");
        pthread_mutex_lock(&status_mutex);
        wifiApData->status = "failure";
        pthread_mutex_unlock(&status_mutex);
        return -1;
    }

    // Check channel number is properly set before starting
    if ((wifiApData->channelNumber < wifiApData->channel.MIN_CHANNEL_VALUE) ||
        (wifiApData->channelNumber > wifiApData->channel.MAX_CHANNEL_VALUE)) {
        AFB_ERROR("Unable to start AP because no valid channel number provided");
        pthread_mutex_lock(&status_mutex);
        wifiApData->status = "failure";
        pthread_mutex_unlock(&status_mutex);
        return -2;
    }

    // Check and resolve conflicts with Network Manager
    check_and_resolve_conflicts_with_NM(wifiApData);

    // Check if firewalld is running and allow dhcp traffic
    check_if_firewalld_running_and_allow_dhcp_traffic(wifiApData);

    // Create hostapd.conf file in /tmp
    if (GenerateHostApConfFile(wifiApData) != 0) {
        AFB_ERROR("Failed to generate hostapd.conf");
        pthread_mutex_lock(&status_mutex);
        wifiApData->status = "failure";
        pthread_mutex_unlock(&status_mutex);
        return -3;
    }
    else
        AFB_INFO("AP configuration file has been generated");

    char cmd[PATH_MAX];
    snprintf((char *)&cmd, sizeof(cmd), " %s %s %s", wifiApData->wifiScriptPath,
             COMMAND_WIFI_HW_START, wifiApData->interfaceName);

    systemResult = system(cmd);
    /**
     * Returned values:
     *   0: if the interface is correctly mounted
     *  50: if WiFi card is not inserted
     * 127: if WiFi card may not work
     * 100: if driver can not be installed
     *  -1: if the fork() has failed (see man system)
     */

    if (0 == WEXITSTATUS(systemResult)) {
        AFB_INFO("WiFi hardware started correctly");
    }
    // Return value of 50 means WiFi card is not inserted.
    else if (WEXITSTATUS(systemResult) == 50) {
        AFB_ERROR("WiFi card is not inserted");
        pthread_mutex_lock(&status_mutex);
        wifiApData->status = "failure";
        pthread_mutex_unlock(&status_mutex);
        return -4;
    }
    // Return value of 100 means WiFi card may not work.
    else if (WEXITSTATUS(systemResult) == 100) {
        AFB_ERROR("Unable to reset WiFi card");
        pthread_mutex_lock(&status_mutex);
        wifiApData->status = "failure";
        pthread_mutex_unlock(&status_mutex);
        return -5;
    }
    // WiFi card failed to start.
    else {
        AFB_WARNING("Failed to start WiFi AP command \"%s\" systemResult (%d)",
                    COMMAND_WIFI_HW_START, systemResult);
        pthread_mutex_lock(&status_mutex);
        wifiApData->status = "failure";
        pthread_mutex_unlock(&status_mutex);
        return -6;
    }

    AFB_INFO("Started WiFi AP command \"%s\" successfully", COMMAND_WIFI_HW_START);

    // Start Access Point cmd: /bin/hostapd /etc/hostapd.conf
    snprintf((char *)&cmd, sizeof(cmd), " %s %s %s", wifiApData->wifiScriptPath,
             COMMAND_WIFIAP_HOSTAPD_START, wifiApData->interfaceName);

    systemResult = system(cmd);
    if ((!WIFEXITED(systemResult)) || (0 != WEXITSTATUS(systemResult))) {
        AFB_ERROR("WiFi Client Command \"%s\" Failed: (%d)", COMMAND_WIFIAP_HOSTAPD_START,
                  systemResult);
        // Remove generated hostapd.conf file
        remove(WIFI_HOSTAPD_FILE);
        pthread_mutex_lock(&status_mutex);
        wifiApData->status = "failure";
        pthread_mutex_unlock(&status_mutex);
        return -7;
    }

    // create WiFi-ap event thread
    wifiApThreadPtr = CreateThread("WifiApThread", WifiApThreadMainFunc, wifiApData->interfaceName);
    if (!wifiApThreadPtr)
        AFB_ERROR("Unable to create thread!");

    // set thread to joinable
    error = setThreadJoinable(wifiApThreadPtr->threadId);
    if (error)
        AFB_ERROR("Unable to set wifiAp thread as joinable!");

    // add thread destructor
    error = addDestructorToThread(wifiApThreadPtr->threadId, threadDestructorFunc,
                                  wifiApData->wifiScriptPath);
    if (error)
        AFB_ERROR("Unable to add a destructor to the wifiAp thread!");

    // start thread
    error = startThread(wifiApThreadPtr->threadId);
    if (error)
        AFB_ERROR("Unable to start wifiAp thread!");

    pthread_mutex_lock(&status_mutex);
    wifiApData->status = "started";
    pthread_mutex_unlock(&status_mutex);
    AFB_INFO("WiFi AP started correctly");
    return 0;
}

/*******************************************************************************
 *                 start access point verb function                            *
 ******************************************************************************/
static void start(afb_req_t request, unsigned nparams, afb_data_t const *params)
{
    AFB_INFO("WiFi access point start verb function");

    afb_api_t wifiAP = afb_req_get_api(request);
    wifiApT *wifiApData = (wifiApT *)afb_api_get_userdata(wifiAP);
    if (!wifiApData) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST,
                             "Can't get WiFi access point data");
        return;
    }

#ifdef TEST_MODE

    char cmd[PATH_MAX];
    snprintf((char *)&cmd, sizeof(cmd), "%s %s", wifiApData->wifiScriptPath,
             COMMAND_GET_VIRTUAL_INTERFACE_NAME);

    FILE *cmdPipePtr = popen(cmd, "r");
    char *interfaceName = malloc(PATH_MAX);
    if (NULL != fgets(interfaceName, PATH_MAX - 1, cmdPipePtr)) {
        size_t interfaceSize = strlen(interfaceName);
        if (interfaceName[interfaceSize - 1] == '\n') {
            interfaceSize--;
        }
        memcpy(wifiApData->interfaceName, interfaceName, interfaceSize);
        free(interfaceName);
        AFB_DEBUG("IFACE : %s", wifiApData->interfaceName);
    }

#endif

    pthread_mutex_lock(&status_mutex);  // status lock for reading
    int string_status =
        (wifiApData->status == NULL) ? -1 : strncmp(wifiApData->status, "started", 7);
    pthread_mutex_unlock(&status_mutex);  // status lock for other writing

    if (string_status != 0) {
        int error = startAp(wifiApData);

        if (!error) {
            AFB_INFO("WiFi AP started correctly");
            afb_req_reply_string(request, 0, "Access point started successfully");
            return;
        }

        switch (error) {
        case -1:
            afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "No valid SSID provided");
            break;
        case -2:
            afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST,
                                 "No valid channel number provided");
            break;
        case -3:
            afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST,
                                 "Failed to generate hostapd.conf");
            break;
        case -4:
            afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "WiFi card is not inserted");
            break;
        case -5:
            afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Unable to reset WiFi card");
            break;
        case -6:
            afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST,
                                 "Failed to start WiFi AP command");
            break;
        case -7:
            afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Failed to start hostapd!");
            break;
        case -8:
            afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Failed to start Dnsmasq!");
            break;
        case -9:
            afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST,
                                 "Failed to clean previous wifiAp configuration!");
            break;
        default:
            afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST,
                                 "Unspecified internal error\n");
        }
    }
}
/*******************************************************************************
 *               stop access point verb function                               *
 ******************************************************************************/

static void stop(afb_req_t request, unsigned nparams, afb_data_t const *params)
{
    int status;

    afb_api_t wifiAP = afb_req_get_api(request);

    wifiApT *wifiApData = (wifiApT *)afb_api_get_userdata(wifiAP);
    if (!wifiApData) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST,
                             "Can't get WiFi access point data");
        return;
    }

    char cmd[PATH_MAX];

    snprintf((char *)&cmd, sizeof(cmd), "%s %s %s", wifiApData->wifiScriptPath,
             COMMAND_WIFIAP_HOSTAPD_STOP, wifiApData->interfaceName);

    status = system(cmd);
    if ((!WIFEXITED(status)) || (0 != WEXITSTATUS(status))) {
        AFB_ERROR("WiFi AP Command \"%s\" Failed: (%d)", COMMAND_WIFIAP_HOSTAPD_STOP, status);
        goto onErrorExit;
    }

    snprintf((char *)&cmd, sizeof(cmd), "%s %s %s", wifiApData->wifiScriptPath,
             COMMAND_WIFI_HW_STOP, wifiApData->interfaceName);

    status = system(cmd);
    if ((!WIFEXITED(status)) || (0 != WEXITSTATUS(status))) {
        AFB_ERROR("WiFi AP Command \"%s\" Failed: (%d)", COMMAND_WIFI_HW_STOP, status);
        goto onErrorExit;
    }

    if (wifiApThreadPtr) {
        /* Terminate the created thread */
        if (0 != cancelThread(wifiApThreadPtr->threadId)) {
            AFB_ERROR("No WiFi client event thread found\n");
        }
        else if (0 != JoinThread(wifiApThreadPtr->threadId, NULL)) {
            goto onErrorExit;
        }
    }
    pthread_mutex_lock(&status_mutex);
    wifiApData->status = "stopped";
    pthread_mutex_unlock(&status_mutex);
    afb_req_reply_string(request, 0, "Access Point was stoped successfully");
    return;

onErrorExit:
    pthread_mutex_lock(&status_mutex);
    wifiApData->status = "failure";
    pthread_mutex_unlock(&status_mutex);
    afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Unspecified internal error\n");
    return;
}

/*******************************************************************************
 *               set the WiFi access point's host name                         *
 ******************************************************************************/
static void setHostName(afb_req_t request, unsigned nparams, afb_data_t const *params)
{
    // check params count
    if (nparams != 1) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Only one argument required");
        return;
    }

    // convert argument to afb_data string
    afb_data_t hostname_param;
    if (afb_data_convert(params[0], AFB_PREDEFINED_TYPE_STRINGZ, &hostname_param)) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Bad data type");
        return;
    }

    // check string size (must be 1 or more)
    char *hostname_string = (char *)afb_data_ro_pointer(hostname_param);
    if (strlen(hostname_string) < 1) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST,
                             "Hostname must be one character or more");
        return;
    }

    // retrieve api userdata
    wifiApT *wifi_ap_data = (wifiApT *)afb_api_get_userdata(afb_req_get_api(request));

    // because new hostname may be longer than old hostname
    size_t new_size = afb_data_size(hostname_param);
    char *new_hostname = realloc(wifi_ap_data->hostName, new_size);
    if (new_hostname == NULL) {
        afb_req_reply_string(request, AFB_ERRNO_OUT_OF_MEMORY, "Failed to realloc hostname");
        return;
    }
    wifi_ap_data->hostName = new_hostname;

    // copy new hostname
    strncpy(wifi_ap_data->hostName, hostname_string, new_size);

    AFB_REQ_INFO(request, "hostname was set successfully to %s", wifi_ap_data->hostName);
    afb_req_reply_string(request, 0, "hostname set successfully");
}

/*******************************************************************************
 *               set the WiFi access point domain name                         *
 ******************************************************************************/
static void setDomainName(afb_req_t request, unsigned nparams, afb_data_t const *params)
{
    if (nparams != 1) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Only one argument required");
        return;
    }

    afb_data_t domain_name_param;
    if (afb_data_convert(params[0], AFB_PREDEFINED_TYPE_STRINGZ, &domain_name_param)) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Bad data type");
        return;
    }

    char *domain_name_string = (char *)afb_data_ro_pointer(domain_name_param);
    if (strlen(domain_name_string) < 1) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST,
                             "Domain name must be one character or more");
        return;
    }

    wifiApT *wifi_ap_data = (wifiApT *)afb_api_get_userdata(afb_req_get_api(request));

    // because new domainName may be longer than old domainName
    size_t new_size = afb_data_size(domain_name_param);
    char *new_domain_name = realloc(wifi_ap_data->domainName, new_size);
    if (new_domain_name == NULL) {
        afb_req_reply_string(request, AFB_ERRNO_OUT_OF_MEMORY, "Failed to realloc domain name");
        return;
    }
    wifi_ap_data->domainName = new_domain_name;

    strncpy(wifi_ap_data->domainName, domain_name_string, new_size);

    AFB_REQ_INFO(request, "domain name was set successfully to %s", wifi_ap_data->domainName);
    afb_req_reply_string(request, 0, "domain name set successfully");
}

/*******************************************************************************
 *               set the WiFi access point interface name                      *
 ******************************************************************************/
static void setInterfaceName(afb_req_t request, unsigned nparams, afb_data_t const *params)
{
    if (nparams != 1) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Only one argument required");
        return;
    }

    afb_data_t interface_name_param;
    if (afb_data_convert(params[0], AFB_PREDEFINED_TYPE_STRINGZ, &interface_name_param)) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Bad data type");
        return;
    }

    char *interface_name_string = (char *)afb_data_ro_pointer(interface_name_param);
    if (strlen(interface_name_string) < 1) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST,
                             "Interface name must be one character or more");
        return;
    }

    wifiApT *wifi_ap_data = (wifiApT *)afb_api_get_userdata(afb_req_get_api(request));

    // because new interface name may be longer than old interface name
    size_t new_size = afb_data_size(interface_name_param);
    char *new_interface_name = realloc(wifi_ap_data->interfaceName, new_size);
    if (new_interface_name == NULL) {
        afb_req_reply_string(request, AFB_ERRNO_OUT_OF_MEMORY, "Failed to realloc interface name");
        return;
    }
    wifi_ap_data->interfaceName = new_interface_name;

    strncpy(wifi_ap_data->interfaceName, interface_name_string, new_size);

    AFB_REQ_INFO(request, "interface name was set successfully to %s", wifi_ap_data->interfaceName);
    afb_req_reply_string(request, 0, "interface name set successfully");
}

/*******************************************************************************
 *               set the WiFi access point SSID                                *
 ******************************************************************************/
static void setSsid(afb_req_t request, unsigned nparams, afb_data_t const *params)
{
    if (nparams != 1) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Only one argument required");
        return;
    }

    afb_data_t ssid_param;
    if (afb_data_convert(params[0], AFB_PREDEFINED_TYPE_STRINGZ, &ssid_param)) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Bad data type");
        return;
    }

    wifiApT *wifi_ap_data = (wifiApT *)afb_api_get_userdata(afb_req_get_api(request));

    AFB_INFO("Set SSID");

    char *ssid_string = (char *)afb_data_ro_pointer(ssid_param);
    if (strlen(ssid_string) < 1) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST,
                             "SSID must be one character or more");
        return;
    }

    if (setSsidParameter(wifi_ap_data, ssid_string)) {
        AFB_REQ_INFO(request, "SSID was set successfully to %s", wifi_ap_data->ssid);
        afb_req_reply_string(request, 0, "SSID set successfully");
    }
    else {
        char err_message[64];  // enough for message
        snprintf(err_message, sizeof(err_message),
                 "Wi-Fi - SSID length exceeds (MAX_SSID_LENGTH = %d)!", MAX_SSID_LENGTH);
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, err_message);
        return;
    }
}

/*******************************************************************************
 *                     set access point passphrase                             *
 ******************************************************************************/

static void setPassPhrase(afb_req_t request, unsigned nparams, afb_data_t const *params)
{
    AFB_INFO("Set Passphrase");

    if (nparams != 1) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Only one argument required");
        return;
    }

    afb_data_t passphrase_param;
    if (afb_data_convert(params[0], AFB_PREDEFINED_TYPE_STRINGZ, &passphrase_param)) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Bad data type");
        return;
    }

    char *passphrase = (char *)afb_data_ro_pointer(passphrase_param);
    if (strlen(passphrase) < 1) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST,
                             "Passphrase must be one character or more");
        return;
    }

    wifiApT *wifi_ap_data = (wifiApT *)afb_api_get_userdata(afb_req_get_api(request));

    // FIXME: check return code (here it's 0) but for previous setSsidParameter() function it
    // was 1...
    if (setPassPhraseParameter(wifi_ap_data, passphrase) == 0) {
        AFB_REQ_INFO(request, "Passphrase was set successfully");
        afb_req_reply_string(request, 0, "Passphrase set successfully!");
    }
    else {
        char err_message[70];  // enough for message
        snprintf(err_message, sizeof(err_message),
                 "Wi-Fi - PassPhrase with Invalid length (MAX_PASSPHRASE_LENGTH = %d)!",
                 MAX_PASSPHRASE_LENGTH);
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, err_message);
        return;
    }
}

/*******************************************************************************
 *           set if access point announce its presence or not                  *
 ******************************************************************************/

static void setDiscoverable(afb_req_t request, unsigned nparams, afb_data_t const *params)
{
    AFB_INFO("Set Discoverable");

    if (nparams != 1) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Only one argument required");
        return;
    }

    afb_data_t discoverable_param;
    if (afb_data_convert(params[0], AFB_PREDEFINED_TYPE_BOOL, &discoverable_param)) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Bad data type");
        return;
    }

    bool *discoverable_bool = (bool *)afb_data_ro_pointer(discoverable_param);
    // FIXME: check with afb-client the behavior if integer or boolean (how it works)

    wifiApT *wifi_ap_data = (wifiApT *)afb_api_get_userdata(afb_req_get_api(request));
    wifi_ap_data->discoverable = discoverable_bool;

    AFB_REQ_INFO(request, "AP is set as discoverable");
    afb_req_reply_string(request, 0, "AP discoverability was set successfully");
}

/*******************************************************************************
 *           set the IEEE standard to use for the access point                 *
 ******************************************************************************/

static void setIeeeStandard(afb_req_t request, unsigned nparams, afb_data_t const *params)
{
    if (nparams != 1) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Only one argument required");
        return;
    }

    wifiApT *wifi_ap_data = (wifiApT *)afb_api_get_userdata(afb_req_get_api(request));

    afb_data_t IeeeStandard_param;
    // json_object_get_int() previously used (V3) returns int32 so we'll use the same here
    // setIeeeStandardParameter() is only using 8 bits so unsigned 32 bits is the closest we have in
    // the framework unsigned 32 bits because of positive value wanted
    if (afb_data_convert(params[0], AFB_PREDEFINED_TYPE_U32, &IeeeStandard_param)) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Bad data type");
        return;
    }

    int *stdMask = (int *)afb_data_ro_pointer(IeeeStandard_param);

    AFB_INFO("Set IeeeStdBitMask : 0x%X", *stdMask);
    // Hardware mode should be set.
    int ieee_check = setIeeeStandardParameter(wifi_ap_data, *stdMask);

    switch (ieee_check) {
    case -1:
        AFB_WARNING("No hardware mode is set");
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "No hardware mode is set");
        break;
    // Hardware mode should be exclusive.
    case -2:
        AFB_WARNING("Only one hardware mode can be set");
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST,
                             "Only one hardware mode can be set");
        break;
    case -3:
        AFB_WARNING("ieee80211ac=1 only works with hw_mode=a");
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST,
                             "ieee80211ac=1 only works with hw_mode=a");
        break;
    case -4:
        AFB_WARNING("ieee80211h=1 only works with ieee80211d=1");
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST,
                             "ieee80211h=1 only works with ieee80211d=1");
        break;
    case 0:
        AFB_REQ_INFO(request, "IeeeStdBitMask was set successfully");
        afb_req_reply_string(request, 0, "stdMask is set successfully");
        break;
    default:
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Parameter is invalid!");
        break;
    }
}

/*******************************************************************************
 *           get the IEEE standard used for the access point                   *
 ******************************************************************************/

static void getIeeeStandard(afb_req_t request, unsigned nparams, afb_data_t const *params)
{
    AFB_INFO("Getting IEEE standard ...");

    if (nparams != 1) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Only one argument required");
        return;
    }

    wifiApT *wifi_ap_data = (wifiApT *)afb_api_get_userdata(afb_req_get_api(request));
    char ieee_standard[64];
    snprintf(ieee_standard, sizeof(ieee_standard), "IEEE standard for WiFiAP is %i",
             wifi_ap_data->IeeeStdMask);
    afb_req_reply_string_copy(request, 0, ieee_standard, strlen(ieee_standard) + 1);
}

/*****************************************************************************************
 *                               Get the number of clients connected to the access point *
 *****************************************************************************************
 * @return success if the function succeeded
 * @return failed request if there is no more AP:s found or the function failed
 *****************************************************************************************/

static void getAPnumberClients(afb_req_t request, unsigned nparams, afb_data_t const *params)
{
    static FILE *IwStationPipePtr = NULL;
    int numberClientsConnectedAP = 0;
    char line[PATH_MAX];

    wifiApT *wifi_ap_data = (wifiApT *)afb_api_get_userdata(afb_req_get_api(request));

    AFB_INFO("Getting the number of clients of the access point ...");

    char cmd[PATH_MAX];
    snprintf((char *)&cmd, sizeof(cmd), " iw dev %s station dump", wifi_ap_data->interfaceName);

    IwStationPipePtr = popen(cmd, "r");

    if (NULL == IwStationPipePtr) {
        AFB_ERROR("Failed to run command:\"%s\" errno:%d %s", COMMAND_WIFI_SET_EVENT, errno,
                  strerror(errno));
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST,
                             "Failed to get the number of clients connected to an access point!");
        return;
    }

    // Read the output one line at a time - output it.
    while (NULL != fgets(line, sizeof(line) - 1, IwStationPipePtr)) {
        AFB_DEBUG("PARSING:%s: len:%d", line, (int)strnlen(line, sizeof(line) - 1));
        if (NULL != strstr(line, "Station ")) {
            numberClientsConnectedAP++;
        }
    }

    char AP_number_clients[64];
    snprintf(AP_number_clients, sizeof(AP_number_clients), "Number of clients: %i",
             numberClientsConnectedAP);
    afb_req_reply_string(request, 0, AP_number_clients);
}

/*****************************************************************************************
 *                                               Get the status of the Wifi access point *
 *****************************************************************************************
 * @return the status of the WiFi access point
 * @return failed request if there is no status variable
 ****************************************************************************************/

static void getWifiApStatus(afb_req_t request, unsigned nparams, afb_data_t const *params)
{
    AFB_INFO("Getting the status of the access point ...");

    wifiApT *wifi_ap_data = (wifiApT *)afb_api_get_userdata(afb_req_get_api(request));

    pthread_mutex_lock(&status_mutex);
    const char *status = wifi_ap_data->status;
    pthread_mutex_unlock(&status_mutex);

    if (status == NULL) {
        afb_req_reply_string(request, AFB_ERRNO_BAD_STATE, "WiFi Access Point status is unknown!");
        return;
    }

    char AP_status[64];
    snprintf(AP_status, sizeof(AP_status), "WiFiAP status: %s", status);
    afb_req_reply_string(request, 0, AP_status);
}

/*******************************************************************************
 *                 restart access point verb function                          *
 ******************************************************************************/
static void restart(afb_req_t request, unsigned nparams, afb_data_t const *params)
{
    int systemResult;
    AFB_INFO("Restarting AP ...");

    wifiApT *wifi_ap_data = (wifiApT *)afb_api_get_userdata(afb_req_get_api(request));

    const char *DnsmasqConfigFileName = "/tmp/dnsmasq.wlan.conf";
    const char *HostConfigFileName = "/tmp/add_hosts";

    if (checkFileExists(DnsmasqConfigFileName) || checkFileExists(HostConfigFileName)) {
        AFB_WARNING("Cleaning previous configuration for AP!");
        char cmd[PATH_MAX];
        snprintf((char *)&cmd, sizeof(cmd), "%s %s %s", wifi_ap_data->wifiScriptPath,
                 COMMAND_WIFIAP_HOSTAPD_STOP, wifi_ap_data->interfaceName);
        // stop WiFi Access Point
        systemResult = system(cmd);
        if ((!WIFEXITED(systemResult)) || (0 != WEXITSTATUS(systemResult))) {
            AFB_ERROR("WiFi AP Command \"%s\" Failed: (%d)", COMMAND_WIFIAP_HOSTAPD_STOP,
                      systemResult);

            pthread_mutex_lock(&status_mutex);
            wifi_ap_data->status = "failure";
            pthread_mutex_unlock(&status_mutex);
            return;
        }
        // Start WiFi Access Point
        if (startAp(wifi_ap_data) < 0) {
            AFB_ERROR("Failed to start Wifi Access Point correctly!");
        }
    }
}

/*******************************************************************************
 *               set the number of WiFi access point channel                   *
 ******************************************************************************/
static void setChannel(afb_req_t request, unsigned nparams, afb_data_t const *params)
{
    AFB_INFO("Set channel number");

    wifiApT *wifi_ap_data = (wifiApT *)afb_api_get_userdata(afb_req_get_api(request));

    if (nparams != 1) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Only one argument required");
        return;
    }

    afb_data_t channel_param;
    if (afb_data_convert(params[0], AFB_PREDEFINED_TYPE_U32, &channel_param)) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Bad data type");
        return;
    }

    uint16_t *channelNumber = (uint16_t *)afb_data_ro_pointer(channel_param);

    if (setChannelParameter(wifi_ap_data, *channelNumber)) {
        AFB_INFO("Channel number was set successfully to %d", wifi_ap_data->channelNumber);
        afb_req_reply_string(request, 0, "Channel number was set successfully");
    }
    else {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST,
                             "Invalid parameter for channel number");
        return;
    }
}

/*******************************************************************************
 *                     set access point security protocol                      *
 ******************************************************************************/

static void setSecurityProtocol(afb_req_t request, unsigned nparams, afb_data_t const *params)
{
    AFB_INFO("Set security protocol");

    if (nparams != 1) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Only one argument required");
        return;
    }

    afb_data_t security_protocol_param;
    if (afb_data_convert(params[0], AFB_PREDEFINED_TYPE_STRINGZ, &security_protocol_param)) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Bad data type");
        return;
    }

    char *security_protocol_string = (char *)afb_data_ro_pointer(security_protocol_param);
    if (strlen(security_protocol_string) < 1) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST,
                             "Domain name must be one character or more");
        return;
    }

    wifiApT *wifi_ap_data = (wifiApT *)afb_api_get_userdata(afb_req_get_api(request));

    if (setSecurityProtocolParameter(wifi_ap_data, security_protocol_string) == 0) {
        afb_req_reply_string(request, 0, "Security parameter was set to none!");
    }
    else if (setSecurityProtocolParameter(wifi_ap_data, security_protocol_string) == 1) {
        afb_req_reply_string(request, 0, "Security parameter was set to WPA2!");
    }
    else {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Parameter is invalid!");
        return;
    }
}

/*******************************************************************************
 *                     set access point pre-shared key                         *
 ******************************************************************************/
static void SetPreSharedKey(afb_req_t request, unsigned nparams, afb_data_t const *params)
{
    AFB_INFO("Set preSharedKey");

    if (nparams != 1) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Only one argument required");
        return;
    }

    afb_data_t presharedkey_param;
    if (afb_data_convert(params[0], AFB_PREDEFINED_TYPE_STRINGZ, &presharedkey_param)) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Bad data type");
        return;
    }

    char *presharedkey_string = (char *)afb_data_ro_pointer(presharedkey_param);
    if (strlen(presharedkey_string) < 1) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST,
                             "Domain name must be one character or more");
        return;
    }

    wifiApT *wifi_ap_data = (wifiApT *)afb_api_get_userdata(afb_req_get_api(request));

    if (presharedkey_string != NULL) {
        if (setPreSharedKeyParameter(wifi_ap_data, presharedkey_string) == 0) {
            AFB_INFO("PreSharedKey was set successfully to %s", wifi_ap_data->presharedKey);
            afb_req_reply_string(request, 0, "PreSharedKey was set successfully!");
        }
        else {
            afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST,
                                 "Parameter length is invalid!");
            return;
        }
    }
}

/*******************************************************************************
 *               set the country code to use for access point                  *
 ******************************************************************************/

static void setCountryCode(afb_req_t request, unsigned nparams, afb_data_t const *params)
{
    AFB_INFO("Set country code");

    if (nparams != 1) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Only one argument required");
        return;
    }

    afb_data_t countrycode_param;
    if (afb_data_convert(params[0], AFB_PREDEFINED_TYPE_STRINGZ, &countrycode_param)) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Bad data type");
        return;
    }

    char *countrycode_string = (char *)afb_data_ro_pointer(countrycode_param);
    if (strlen(countrycode_string) < 1) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST,
                             "Domain name must be one character or more");
        return;
    }

    wifiApT *wifi_ap_data = (wifiApT *)afb_api_get_userdata(afb_req_get_api(request));

    if (countrycode_string != NULL) {
        if (setCountryCodeParameter(wifi_ap_data, countrycode_string) == 0) {
            AFB_INFO("country code was set to %s", wifi_ap_data->countryCode);
            afb_req_reply_string(request, 0, "country code was set successfully");
        }
        else {
            afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST,
                                 "Parameter length is invalid!");
            return;
        }
    }
}

/*******************************************************************************
 *               set the max number of clients of access point                 *
 ******************************************************************************/

static void SetMaxNumberClients(afb_req_t request, unsigned nparams, afb_data_t const *params)
{
    AFB_INFO("Set the maximum number of clients");

    if (nparams != 1) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Only one argument required");
        return;
    }

    afb_data_t max_number_clients_param;
    if (afb_data_convert(params[0], AFB_PREDEFINED_TYPE_U32, &max_number_clients_param)) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Bad data type");
        return;
    }

    wifiApT *wifi_ap_data = (wifiApT *)afb_api_get_userdata(afb_req_get_api(request));

    uint32_t *maxNumberClients = (uint32_t *)afb_data_ro_pointer(max_number_clients_param);
    // maxnumberClients defined at 1000 here for our case
    if (setMaxNumberClients(wifi_ap_data, *maxNumberClients) == 0) {
        AFB_NOTICE("The maximum number of clients was set to %i", wifi_ap_data->maxNumberClient);
        afb_req_reply_string(request, 0, "Max Number of clients was set successfully!");
    }
    else {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "The value is out of range");
        return;
    }
}

/*******************************************************************************
 *     Set the access point IP address and client IP  addresses rang           *
 ******************************************************************************/
static void setIpRange(afb_req_t request, unsigned nparams, afb_data_t const *params)
{
    const char *ip_ap, *ip_start, *ip_stop, *ip_netmask;

    if (nparams != 1) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Only one argument required");
        return;
    }

    wifiApT *wifi_ap_data = (wifiApT *)afb_api_get_userdata(afb_req_get_api(request));

    afb_data_t ip_range_param;
    if (afb_data_convert(params[0], AFB_PREDEFINED_TYPE_JSON_C, &ip_range_param)) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Bad data type");
        return;
    }  // FIXME: to replace for all with afb_req_param_convert or check unref...

    json_object *args_json = (json_object *)afb_data_ro_pointer(ip_range_param);
    if (!args_json) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Unable to use JSON arguments");
        return;
    }

    /*
        ip_ap    : Access point's IP address
        ip_start : Access Point's IP address start
        ip_stop  : Access Point's IP address stop
    */

    int error = rp_jsonc_unpack(args_json, "{ss,ss,ss,ss !}", "ip_ap", &ip_ap, "ip_start",
                                &ip_start, "ip_stop", &ip_stop, "ip_netmask", &ip_netmask);
    if (error) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "Invalid JSON format");
        return;
    }

    if (setIpRangeParameters(wifi_ap_data, ip_ap, ip_start, ip_stop, ip_netmask)) {
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST,
                             "Unable to set IP addresses for the Access point");
        return;
    }

    afb_req_reply_string(request, 0, "IP range was set successfully!");
}
/*******************************************************************************
 *		               WiFi Access Point verbs table *
 ******************************************************************************/

static const afb_verb_t verbs[] = {
    {.verb = "start", .callback = start, .info = "start the WiFi access point service"},
    {.verb = "stop", .callback = stop, .info = "stop the WiFi access point service"},
    {.verb = "restart", .callback = restart, .info = "restart the WiFi access point service"},
    {.verb = "setSsid", .callback = setSsid, .info = "set the wifiAp SSID"},
    {.verb = "setInterfaceName",
     .callback = setInterfaceName,
     .info = "set the name of the interface to be used as access point"},
    {.verb = "setHostName", .callback = setHostName, .info = "set the access point's hostname"},
    {.verb = "setDomainName",
     .callback = setDomainName,
     .info = "set the access point domain name"},
    {.verb = "setPassPhrase", .callback = setPassPhrase, .info = "set the wifiAp passphrase"},
    {.verb = "setDiscoverable",
     .callback = setDiscoverable,
     .info = "set if access point announce its presence"},
    {.verb = "setIeeeStandard",
     .callback = setIeeeStandard,
     .info = "set which IEEE standard to use "},
    {.verb = "setChannel", .callback = setChannel, .info = "set which WiFi channel to use"},
    {.verb = "getIeeeStandard",
     .callback = getIeeeStandard,
     .info = "get which IEEE standard is used"},
    {.verb = "setSecurityProtocol",
     .callback = setSecurityProtocol,
     .info = "set which security protocol to use"},
    {.verb = "setPreSharedKey", .callback = SetPreSharedKey, .info = "set the pre-shared key"},
    {.verb = "setIpRange",
     .callback = setIpRange,
     .info = "define the access point IP address and client IP  addresses range"},
    {.verb = "setCountryCode",
     .callback = setCountryCode,
     .info = "set the country code to use for regulatory domain"},
    {.verb = "subscribe", .callback = subscribe, .info = "Subscribe to WiFi-ap events"},
    {.verb = "unsubscribe", .callback = unsubscribe, .info = "Unsubscribe to WiFi-ap events"},
    {.verb = "SetMaxNumberClients",
     .callback = SetMaxNumberClients,
     .info =
         "Set the maximum number of clients allowed to be connected to WiFiAP at the same time"},
    {.verb = "getAPclientsNumber",
     .callback = getAPnumberClients,
     .info = "Get the number of clients connected to the access point"},
    {.verb = "getWifiApStatus",
     .callback = getWifiApStatus,
     .info = "Get the status of the Wifi access point"}};

/*******************************************************************************
 *                                             WiFiap-binding mainctl function *
 ******************************************************************************/
int binding_ctl(afb_api_t api, afb_ctlid_t ctlid, afb_ctlarg_t ctlarg, void *userdata)
{
    switch (ctlid) {
    case afb_ctlid_Init: {
        AFB_API_NOTICE(api, "Binding start ...");

        wifiApT *wifiApData = (wifiApT *)calloc(1, sizeof(wifiApT));
        if (!wifiApData) {
            AFB_API_ERROR(api, "Memory allocation failed");
            return -1;
        }

        CDS_INIT_LIST_HEAD(&wifiApList);
        CDS_INIT_LIST_HEAD(&wifiApData->wifiApListHead);

        // Reading the JSON file
        struct json_object *root, *config;
        root = json_object_from_file(PATH_CONFIG_FILE);
        if (!root) {
            AFB_API_ERROR(api, "Failed to read config file");
            free(wifiApData);
            return -1;
        }

        // Accessing the config section
        if (!json_object_object_get_ex(root, "config", &config)) {
            AFB_API_ERROR(api, "No 'config' section in JSON file");
            json_object_put(root);
            free(wifiApData);
            return -1;
        }

        // Filling the structure from the JSON
        wifiApData->interfaceName =
            strdup(json_object_get_string(json_object_object_get(config, "interfaceName")));
        wifiApData->domainName =
            strdup(json_object_get_string(json_object_object_get(config, "domaine_name")));
        wifiApData->hostName =
            strdup(json_object_get_string(json_object_object_get(config, "hostname")));
        wifiApData->status = strdup("initializing");
        wifiApData->uid = strdup(json_object_get_string(json_object_object_get(config, "uid")));

        strncpy(wifiApData->ip_ap, json_object_get_string(json_object_object_get(config, "ip_ap")),
                sizeof(wifiApData->ip_ap) - 1);
        strncpy(wifiApData->ip_start,
                json_object_get_string(json_object_object_get(config, "ip_start")),
                sizeof(wifiApData->ip_start) - 1);
        strncpy(wifiApData->ip_stop,
                json_object_get_string(json_object_object_get(config, "ip_stop")),
                sizeof(wifiApData->ip_stop) - 1);
        strncpy(wifiApData->ip_netmask,
                json_object_get_string(json_object_object_get(config, "ip_netmask")),
                sizeof(wifiApData->ip_netmask) - 1);
        strncpy(wifiApData->ssid, json_object_get_string(json_object_object_get(config, "ssid")),
                sizeof(wifiApData->ssid) - 1);
        strncpy(wifiApData->passphrase,
                json_object_get_string(json_object_object_get(config, "passphrase")),
                sizeof(wifiApData->passphrase) - 1);
        strncpy(wifiApData->countryCode,
                json_object_get_string(json_object_object_get(config, "countryCode")),
                sizeof(wifiApData->countryCode) - 1);

        const char *security =
            json_object_get_string(json_object_object_get(config, "securityProtocol"));
        if (strcmp(security, "WPA2") == 0) {
            wifiApData->securityProtocol = WIFI_AP_SECURITY_WPA2;
        }  // Add other cases if necessary

        wifiApData->channelNumber =
            json_object_get_int(json_object_object_get(config, "channelNumber"));
        wifiApData->channel.MIN_CHANNEL_VALUE = 1;
        wifiApData->channel.MAX_CHANNEL_VALUE = 13;

        wifiApData->discoverable =
            json_object_get_boolean(json_object_object_get(config, "discoverable"));
        wifiApData->maxNumberClient =
            json_object_get_int(json_object_object_get(config, "maxNumberClient"));
        wifiApData->IeeeStdMask =
            json_object_get_int(json_object_object_get(config, "IeeeStdMask"));

        json_object_put(root);  // Free the JSON memory

        afb_api_set_userdata(api, wifiApData);
        cds_list_add_tail(&wifiApData->wifiApListHead, &wifiApList);

        event_add(api, "client-state");
        AFB_API_NOTICE(api, "Initialization finished");
        break;
    }
    default:
        break;
    }
    return 0;
}

const afb_binding_t afbBindingExport = {
    .api = "wifiAp",
    .specification = NULL,
    .verbs = verbs,
    .mainctl = binding_ctl,
    .userdata = NULL,
    .provide_class = NULL,
    .require_class = NULL,
    .require_api = NULL,
    .noconcurrency = 0
};
