###########################################################################
# Copyright 2015 - 2025 IoT.bzh
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
###########################################################################

Name: wifiap-binding
Version: 0.1.3
Release: 16%{?dist}
Summary: Provide a Redpesk wifi Access Point Binding
License:  GPLv3
URL: https://git.ovh.iot/redpesk/redpesk-common/wifiap-binding
Source: %{name}-%{version}.tar.gz

%global _afmappdir %{_prefix}/redpesk
%global coverage_dir %{_libexecdir}/redtest/%{name}/coverage_data

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  afb-cmake-modules
BuildRequires:  pkgconfig(json-c)
BuildRequires:  pkgconfig(afb-binding)
BuildRequires:  pkgconfig(librp-utils-json-c)
BuildRequires:  lcov
BuildRequires:  pkgconfig(libsystemd) >= 222
BuildRequires:  pkgconfig(afb-helpers4)
BuildRequires:  userspace-rcu-devel
Requires: afb-binder hostapd dnsmasq

%if 0%{?suse_version}
BuildRequires:  libdb-4_8-devel
%else
BuildRequires:  libdb-devel
%endif

Requires:       afb-binder

%description
This binding provide a Redpesk wifi Access Point Binding.

%package redtest
Summary: redtest package (coverage build)
Requires: lcov
%description redtest
This package contains binaries built with coverage instrumentation.

%prep
%autosetup -p 1

%build
# Build (no coverage)
mkdir build-no-coverage && cd build-no-coverage
%cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DAFM_APP_DIR=%{_afmappdir} ..
%cmake_build
cd ..

# Build coverage (with coverage flags)
mkdir build-coverage && cd build-coverage
%cmake \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS="--coverage -fPIC" \
  -DCMAKE_CXX_FLAGS="--coverage -fPIC" \
  -DAFM_APP_DIR=%{coverage_dir} ..
%cmake_build
cd ..

%install
# Install (base package)
cd build-no-coverage
%cmake_install
cd ..

# Install coverage build (for redtest package)
cd build-coverage
%cmake_install

# Copy the coverage files (.gcno) into the coverage_data directory for redtest
find . -name "*.gcno" -exec cp --parents {} %{buildroot}%{coverage_dir}/ \;
cd ..

%files
%defattr(-,root,root,-)
%dir %{_afmappdir}/%{name}
%{_afmappdir}/%{name}/lib/
%{_afmappdir}/%{name}/.rpconfig/
%{_afmappdir}/%{name}/etc/wifiap-config.json
%{_afmappdir}/%{name}/var/wifi_setup.sh
%{_afmappdir}/%{name}/var/wifi_setup_test.sh

%files redtest
%defattr(-,root,root)
%{_libexecdir}/redtest/%{name}/run-redtest
%{_libexecdir}/redtest/%{name}/tests.py
%{coverage_dir}
