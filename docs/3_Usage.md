# How to use `wifiap-binding`

> Please note that this binding requires a WiFi interface (like `wlan0`) on the target.
> It's possible to emulate the interface, please refer to the tests section below.

## Create/Update a WiFi JSON configuration corresponding to your wanted setup

### What you need to set in this configuration file to start your access point

* For information about `metadata` section, please look at [controller documentation]({% chapter_link libappcontroller-guides.installation %})

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
  * `ip_netmask` key is used to set the IP address mask of the Access Point (Mandatory if you want to start the access point at init).

## Running the binding

### Run based on installed build sources

```bash
afb-binder \
--binding=/usr/local/redpesk/wifiap-binding/lib/wifiap-binding.so:/usr/local/redpesk/wifiap-binding/etc/wifiap-config.json \
--port=1234 \
--tracereq common \
-vvv
```

### Run based on redpesk package

```
afm-util start wifiap-binding
```

### Connect to binding

Connect to wifiAp binding using `afb-client` or the web interface through (`http://[IP]:[PORT]/devtools/`)

```bash
afb-client -H ws://localhost:1234/api
```

#### Get information of available verbs

```bash
afb-client -H ws://localhost:1234/api
wifiAp info
ON-REPLY 1:wifiAp/info: OK
{
  "jtype":"afb-reply",
  "request":{
    "status":"success",
    "code":0
  },
  "response":{
    "metadata":{
      "uid":"wifiap-binding",
      "info":"Provide a Redpesk wifi Access Point Binding",
      "version":"1.0"
    },
    "groups":[
      {
        "uid":"general",
        "info":"Verbs related to general uses of the binding",
        "verbs":[
          {
            "uid":"info",
            "info":"info verb to retrieve all the available verbs",
          },
          {

[...]

```

#### Set the SSID

```bash
wifiAp setSsid testAP
```

Output example:

```bash
ON-REPLY 2:wifiAp/setSsid: OK
{
  "jtype":"afb-reply",
  "request":{
    "status":"success",
    "code":0
  }
}
```

#### Set the channel

```bash
wifiAp setChannel 1
```

Output example:

```bash
ON-REPLY 3:wifiAp/setChannel: OK
{
  "jtype":"afb-reply",
  "request":{
    "status":"success",
    "code":0
  }
}
```

#### Set the security protocol

```bash
wifiAp setSecurityProtocol none
```

Output example:

```bash
ON-REPLY 5:wifiAp/setSecurityProtocol: OK
{
  "jtype":"afb-reply",
  "request":{
    "status":"success",
    "code":0
  }
}
```

By setting this parameter like that, no password will be required to connect to the access point (not recommended for production usecases).

#### Set the ip addresses range of AP

```bash
wifiAp setIpRange {"ip_ap" : "192.168.2.1", "ip_start" : "192.168.2.10" , "ip_stop" : "192.168.2.100" , "ip_netmask" : "255.255.255.0"}
```

Output example:

```bash
ON-REPLY 6:wifiAp/setIpRange: OK
{
  "jtype":"afb-reply",
  "request":{
    "status":"success",
    "code":0
  }
}
```

You can do the same for the rest of available parameters.

#### Start the AP

```bash
wifiAp start
```

Output example:

```bash
ON-REPLY 7:wifiAp/start: OK
{
  "jtype":"afb-reply",
  "request":{
    "status":"success",
    "code":0
  }
}
```

You can now connect to the WiFi access point from another device.

#### Stop the AP

```bash
wifiAp stop
```

Output example:

```bash
ON-REPLY 8:wifiAp/stop: OK
{
  "jtype":"afb-reply",
  "request":{
    "status":"success",
    "code":0
  }
}
```

You can also subscribe to the event to see the client connection for example.

#### Subscribe to the AP events

```bash
wifiAp subscribe
```

Output example:

```bash
ON-REPLY 11:wifiAp/subscribe: OK
{
  "jtype":"afb-reply",
  "request":{
    "status":"success",
    "code":0
  }
}
```

If a client connects to the access point, you will see it through an event:

```bash
ON-EVENT wifiAp/client-state:
{
  "jtype":"afb-event",
  "event":"wifiAp/client-state",
  "data":{
    "Event":"WiFi client connected",
    "number-client":1
  }
}
```

And the same for the disconnection.

## Emulate WiFi interface

If you hardware doesn't provide a valid WiFi interface, it's possible to use a Kernel module for emulating the access point.

### Launch redpesk x86_64 image using QEMU

To retrieve the image, please follow the [prerequisites]({% chapter_link boards-virtual-doc.qemu %}).

```bash
qemu-system-x86_64    \
-hda "Redpesk-OS.img" \
-enable-kvm \
-m 2048 \
-cpu kvm64 \
-cpu Skylake-Client-v4 \
-smp 4 \
-vga virtio \
-device virtio-rng-pci \
-serial mon:stdio \
-net nic \
-net user,hostfwd=tcp::$PORT_SSH-:22 \
-kernel vmlinuz-6.12.0-54.baseos.rpcorn.x86_64 \
-display none \
-initrd initramfs-6.12.0-54.baseos.rpcorn.x86_64.img \
-append 'console=ttyS0 cgroup_no_v1=all systemd.unified_cgroup_hierarchy=1 rw rootwait security=smack root=LABEL=rootfs loglevel=7'
```

### Installation of additionnal Kernel modules

```
dnf install kernel-modules-internal
```

### Load the module which enables the emulation

```
# modprobe mac80211_hwsim
[ 2102.909618] mac80211_hwsim: initializing netlink
```

### Check your network interfaces

The command `ip a` will show you the virtual WiFi interface:

```bash
5: hwsim0: <BROADCAST,MULTICAST> mtu 1500 qdisc noop state DOWN group default qlen 1000
    link/ieee802.11/radiotap 12:00:00:00:00:00 brd ff:ff:ff:ff:ff:ff
```

So `hwsim0` is the interface name to give as `interfaceName` parameter to the binding.
