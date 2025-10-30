# WiFi access point binding

## Architecture

The Wifi Access point binding is based on the [legato.io](https://legato.io/) Wifi API.

This binding uses `hostapd` for launching the WiFi access point and `dnsmasq` for managing the ip addresses (DHCP).

It exposes a standard set of REST/Websocket APIs, providing a simple yet powerful mechanism to configure and control a WiFi access point dynamically. It's possible to manage network parameters such as:

* Interface name
* Domain name
* Hostname
* SSID and passphrase (or pre-shared key)
* Country code
* Security protocol
* Access point IP address and DHCP range (start, stop, netmask)
* WiFi discoverability
* Maximum number of clients
* IEEE standard mask
* Channel number used by the access point

These parameters are configured via JSON files or runtime API commands.

* Define a config.json with 'script' for access point parameters
* User standard `afb-devtools-ui` or provide a custom HTML5 page.

The binding sends status and event updates asynchronously through websocket events (you can subscribe to these events to see the access point status, the client connection or disconnection...).

## Documentation

* [Installation steps](https://docs.redpesk.bzh/docs/en/master/redpesk-core/wifiap-binding/2_Installation.html)
* [Usage guide](https://docs.redpesk.bzh/docs/en/master/redpesk-core/wifiap-binding/3_Usage.html)

## HTML5 test page

![wifiap-binding-html5](docs/assets/wifiap-binding-devtools.png)