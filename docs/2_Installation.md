# Installation steps

The AFB HTML client is provided by the `afb-ui-devtools` package, but it is not a requirement to run the spawn-binding.

## Installing on redpesk OS

On a redpesk image, `wifiap-binding` is part of redpesk-common and is available on any redpesk installation.

```bash
sudo dnf install wifiap-binding
```

## Rebuilding from source

In some situations, you may want to build the binding from its sources by:

* targeting a not supported environment/distribution.
* changing code to fix bug or propose improvement *(contributions are more than welcome)*

### Install building dependencies

### Mandatory packages

* Declare redpesk repository: (see [doc]({% chapter_link host-configuration-doc.setup-your-build-host %}))

* Regular packages to execute application framework binder, and to load bindings with it
(see [Download Packages for Binder]({% chapter_link afb_binder.getting-the-binder %}))

* Other required packages:
  * json-c
  * librp-utils-json-c
  * userspace-rcu-devel
  * afb-helpers4-devel
  * afb-ui-devtools (if webUI wanted)
* Requested packages to start the binding
  * hostapd
  * dnsmasq

> Note: all previous dependencies should be available out-of-the-box within any good Linux distribution. Note that Debian and Ubuntu use '-dev' in place of '-devel' for package names.

### Download source from git

```bash
git clone https://github.com/redpesk-common/wifiap-binding.git
```

### Build for Linux distribution

```bash
cd wifiap-binding
mkdir build
cd build
cmake ..
make
```

If you want to install the binding from the sources:

```bash
[root@localhost build]# make install
[ 12%] Generating source file from JSON
[ 12%] Built target generate_info_src
Consolidate compiler generated dependencies of target wifiap-utilities
[ 75%] Built target wifiap-utilities
Consolidate compiler generated dependencies of target wifiap-binding
[ 87%] Building C object CMakeFiles/wifiap-binding.dir/src/wifiAp.c.o
[100%] Linking C shared library wifiap-binding.so
[100%] Built target wifiap-binding
Install the project...
-- Install configuration: ""
-- Installing: /usr/local/redpesk/wifiap-binding/lib/wifiap-binding.so
-- Installing: /usr/local/redpesk/wifiap-binding/.rpconfig/manifest.yml
-- Installing: /usr/local/redpesk/wifiap-binding/etc/wifiap-config.json
-- Installing: /usr/local/redpesk/wifiap-binding/var/wifi_setup.sh
-- Installing: /usr/local/redpesk/wifiap-binding/var/wifi_setup_test.sh
-- Installing: /usr/libexec/redtest/wifiap-binding/run-redtest
-- Installing: /usr/libexec/redtest/wifiap-binding/tests.py
```

### Run a test from building tree

```bash
afb-binder --binding=./build/wifiap-binding.so:./etc/wifiap-config.json --tracereq common -vvv 
```
