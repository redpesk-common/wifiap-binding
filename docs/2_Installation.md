# Installation

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
  * afb-helpers4-dev
  * lua
  * afb-ui-devtools (if webUI wanted)
* Requested packages to start the binding
  * hostapd
  * dnsmasq

### Ubuntu/Debian

```bash
sudo apt install afb-binder \
                 afb-client \
                 afb-binding-dev \
                 afb-helpers4-dev \
                 cmake gcc g++ \
                 afb-cmake-modules \
                 libjson-c-dev \
                 afb-libcontroller-dev \
                 libmicrohttpd-dev \
                 liblua5.3-dev \
                 afb-libhelpers-dev \
                 hostapd dnsmasq \
                 liburcu-dev
```

### Fedora/CentOS/redpesk

```bash
sudo dnf install afb-binder \
                 afb-client \
                 afb-binding-devel \
                 afb-helpers4-devel \
                 cmake gcc g++ \
                 afb-cmake-modules \
                 json-c-devel \
                 afb-libcontroller-devel \
                 libmicrohttpd-devel \
                 afb-libhelpers-devel \
                 lua-devel \
                 hostapd dnsmasq \
                 userspace-rcu-devel
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
