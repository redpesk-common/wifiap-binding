# Installation

## Pre-requisites

This binding uses **hostapd** for launching the access point and **dnsmasq**
for managing the ip addresses

## Rebuilding from source

### Mandatory packages

* Declare redpesk repository: (see [doc]({% chapter_link host-configuration-doc.setup-your-build-host %}))

* Regular packages to execute application framework binder, and to load bindings with it
(see [Download Packages for Binder]({% chapter_link afb_binder.getting-the-binder %}))

* Requested packages to compile the binding:
  * afb-cmake-modules
  * json-c
  * libsystemd
  * afb-binder
  * libmicrohttpd
  * afb-libhelpers-dev
  * afb-libcontroller-dev
  * lua
* Requested packages to start the binding
  * hostapd
  * dnsmasq

### Ubuntu/Debian

```bash
sudo apt install afb-binder afb-client afb-binding-dev cmake afb-cmake-modules libjson-c-dev libmicrohttpd-dev liblua5.3-dev afb-libhelpers-dev afb-libcontroller-dev hostapd dnsmasq liburcu-dev
```

### Fedora

```bash
sudo dnf install  afb-binder afb-client afb-binding-devel cmake gcc g++ afb-cmake-modules json-c-devel libmicrohttpd-devel afb-libhelpers-devel afb-libcontroller-devel lua-devel hostapd dnsmasq liburcu-dev
```

### Build for Linux distribution

```bash
cd wifiap-binding
mkdir -p build
cd build
cmake -DBUILD_TEST_WGT=TRUE ..
make
make widget
```

## Test

If you want to run the test and the code coverage just execute code:

```bash
cd wifiap-binding
cd build
afm-test package package-test/ -m SERVICE
```
