# Wifi Access Point Service

--------------------------------------------------------------------------------

* Object : This WiFi service API provides  WiFi access point setup
* Status : Working
* Author : Salma Raiss (salma.raiss@iot.bzh)
* Date   : November-2020

## Pre-requisites

This binding uses **hostapd** for launching the access point and **dnsmasq**
for managing the ip addresses

### Mandatory packages

* Regular packages to execute application framework binder, and to load bindings with it
(see [Download Packages for Binder](https://docs.automotivelinux.org/docs/en/master/devguides/reference/2-download-packages.html)

* Requested packeges to compile the binding:
  * agl-cmake-apps-module
  * json-c
  * libsystemd
  * afb-daemon
  * libmicrohttpd
  * afb-helpers
  * ctl-utilities
  * lua
* Requested packages to start the binding
  * hostapd
  * dnsmasq


### Ubuntu/Debian

```bash
sudo apt install agl-app-framework-binder-dev cmake agl-cmake-apps-module-bin libjson-c-dev agl-libmicrohttpd-dev liblua5.3-dev agl-libafb-helpers-dev agl-libappcontroller-dev hostapd dnsmasq liburcu-dev
```

### Fedora

```bash
sudo dnf install agl-app-framework-binder-devel cmake agl-cmake-apps-module libjson-c-dev libmicrohttpd-devel uuid-devel alsa-lib-devel agl-libafb-helpers-devel agl-libappcontroller-devel lua-devel hostapd dnsmasq liburcu-dev
```

### Create/Update a wifi json configuration corresponding to your wanted setup

#### What you need to set in this configuration file to start your access point

* For information about `metadata` section, please look at [controller documentation](https://docs.automotivelinux.org/docs/en/master/devguides/reference/ctrler/controllerConfig.html)

* `config` section:
This section is used to define the wifi access point parameters needed to start
the service.
Available keys:
  * `startAtInit` key is an optional key to specify if you want to start the
  access point using the configuration provided in the configuration file at
  the binding init.
  * `interfaceName` key is the name of the interface to use as access point (
  it's a mandatory key).
  * `ssid` key is the Service Set Identification (SSID) of the access point.
  It's mandatory if you want to start the binding at init otherwise you can set it using the **setSsid** verb.
  * `channelNumber` key is used to set which Channel to use.
   Some legal restrictions might apply for your region.
    * The channel number must be between 1 and 14 for IEEE 802.11b/g.
    * The channel number must be between 7 and 196 for IEEE 802.11a.
    * The channel number must be between 1 and 6 for IEEE 802.11ad.
  * `discoverable` key is used to set if the Access Point should announce its presence,
  otherwise it wil be hidden.
  * `IeeeStdMask` key is used to set which IEEE standard to use.
  * `securityProtocol` key is used to set the security protocol to use. It can either
  be set to *none* or *WPA2*.
  * `passphrase` key is used to generate the PSK.
  * `preSharedKey` key is used if you want to set the pre-SharedKey (PSK) directly.
  * `countrycode` key is used to set what country code to use for regulatory domain.
  ISO/IEC 3166-1 Alpha-2 code is used.
  * `maxNumberClient` key is used to set number of maximally allowed clients to connect to the Access Point at the same time.
  * `ip_ap` key is used to set the IP address of the Access Point (Mandatory if you want to start the access point at init).
  * `ip_start` key is used to set the start IP address of the Access Point (Mandatory if you want to start the access point at init).
  * `ip_stop` key is used to set the stop IP address of the Access Point (Mandatory if you want to start the access point at init).

### Native build (Ubuntu, Fedora)

**_@note_** : you need to verify that in your configuration file you got the right name of
the interface to use as access point.

Build your binding from shell:

```bash
mkdir build
cd build
cmake ..
make
```

### Runing the binding

Currently, the binding configuration file found in the path specified with CONTROL_CONFIG_PATH environment variable (the configuration file should begin with wifi-wifiap-binding-).

#### Run from shell (native execution)

```bash
afb-daemon --name=afbd-wifiap-binding --port=1234  --ldpaths=package --workdir=. --token= -vvv
```

#### Connect to binding

Connect to wifiAp binding using afb-client-demo

```bash
afb-client-demo -H ws://localhost:1234/api?token=
```

##### Set the SSID

```bash
wifiAp setSsid testAP
```

Output example:

```bash
ON-REPLY 1:wifiAp/setSsid: OK
{
  "response":{
    "SSID":"testAp"
  },
  "jtype":"afb-reply",
  "request":{
    "status":"success",
    "info":"SSID set successfully"
  }
}
```

##### Set the channel

```bash
wifiAP setChannel 1
```

Output example:

```bash
ON-REPLY 2:wifiAP/setChannel: OK
{
  "response":{
    "channelNumber":1
  },
  "jtype":"afb-reply",
  "request":{
    "status":"success"
  }
}
```

##### Set the security protocol

```bash
wifiAP setSecurityProtocol none
```

Output example:

```bash
ON-REPLY 3:wifiAp/setSecurityProtocol: OK
{
  "jtype":"afb-reply",
  "request":{
    "status":"success",
    "info":"Security parameter was set to none!"
  }
}
```

##### Set the ip addresses range of AP

```bash
wifiAp setIpRange {"ip_ap" : "192.168.2.1", "ip_start" : "192.168.2.10" , "ip_stop" : "192.168.2.100"}
```

Output example:

```bash
ON-REPLY 4:wifiAp/setIpRange: OK
{
  "jtype":"afb-reply",
  "request":{
    "status":"success",
    "info":"IP range was set successfully!"
  }
}
```

##### Start the AP

```bash
wifiAp start
```

Output example:

```bash
ON-REPLY 5:wifiAp/start: OK
{
  "jtype":"afb-reply",
  "request":{
    "status":"success",
    "info":"Access point started successfully"
  }
}
```

You can now connect to the wifi access point from another device

##### Stop the AP

```bash
wifiAp stop
```

Output example:

```bash
ON-REPLY 6:wifiAp/stop: OK
{
  "jtype":"afb-reply",
  "request":{
    "status":"success",
    "info":"Access Point was stoped successfully"
  }
}
```

### Tests

To test the binding without any physical device, you can use the **mac80211_hwsim** kernel module.

```bash
sudo modprobe mac80211_hwsim radios=2
```

To get the name of the virtual interface generated by the kernel module

```bash
ip -br -c l
```

**_@note_** : You need to change the interface name in the configuration file and rebuild
with DBUILD_TEST_WGT option set at true

```bash
mkdir build
cd build
cmake -DBUILD_TEST_WGT=TRUE ..
make
make widget
```

#### launch lua tests

```bash
afm-test package package-test/ -m SERVICE
```
