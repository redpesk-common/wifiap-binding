###########################################################################
# Copyright (C) 2015-2024 "IoT.bzh"
# Author: Valentin Geffroy <valentin.geffroy@iot.bzh>
#
# $RP_BEGIN_LICENSE$
# Commercial License Usage
#  Licensees holding valid commercial IoT.bzh licenses may use this file in
#  accordance with the commercial license agreement provided with the
#  Software or, alternatively, in accordance with the terms contained in
#  a written agreement between you and The IoT.bzh Company. For licensing terms
#  and conditions see https://www.iot.bzh/terms-conditions. For further
#  information use the contact form at https://www.iot.bzh/contact.
#
# GNU General Public License Usage
#  Alternatively, this file may be used under the terms of the GNU General
#  Public license version 3. This license is as published by the Free Software
#  Foundation and appearing in the file LICENSE.GPLv3 included in the packaging
#  of this file. Please review the following information to ensure the GNU
#  General Public License requirements will be met
#  https://www.gnu.org/licenses/gpl-3.0.html.
# $RP_END_LICENSE$
###########################################################################

cmake_minimum_required(VERSION 3.6)

project(wifiap-binding
        VERSION 2.0.0
        DESCRIPTION "Provide a Redpesk wifi Access Point Binding"
        HOMEPAGE_URL "https://github.com/redpesk-common/wifiap-binding"
        LANGUAGES C
)
set(PROJECT_AUTHOR "Iot-Team")
set(PROJECT_AUTHOR_MAIL "secretariat@iot.bzh")
set(PROJECT_LICENSE "GPLv3.0")

include(GNUInstallDirs)
include(FindPkgConfig)

# Declare options
set(AFM_APP_DIR ${CMAKE_INSTALL_PREFIX}/redpesk CACHE PATH "Applications directory")
set(APP_DIR ${AFM_APP_DIR}/${PROJECT_NAME})

# Check dependencies
pkg_check_modules(deps REQUIRED
    json-c
    afb-binding
    afb-libhelpers
    liburcu
    afb-helpers4
)

# Compile the library wifiap-utilities
add_library(wifiap-utilities STATIC lib/wifi-ap-utilities/wifi-ap-config.c
                                    lib/wifi-ap-utilities/wifi-ap-data.c
                                    lib/wifi-ap-utilities/wifi-ap-thread.c
                                    lib/wifi-ap-utilities/wifi-ap-utilities.c
)
target_include_directories(wifiap-utilities PRIVATE ${deps_INCLUDE_DIRS})
set_target_properties(wifiap-utilities PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Compile the binding
add_compile_options(-Wall -Wextra -Wconversion 
-Wno-unused-parameter -Wno-sign-compare -Wno-sign-conversion 
-Werror=maybe-uninitialized -Werror=implicit-function-declaration 
-ffunction-sections -fdata-sections -Wno-shift-count-overflow 
-Wno-conversion)
add_library(wifiap-binding SHARED src/wifiAp.c)
set_target_properties(wifiap-binding PROPERTIES PREFIX "")
target_include_directories(wifiap-binding PRIVATE ${deps_INCLUDE_DIRS})
target_include_directories(wifiap-binding AFTER PRIVATE lib/wifi-ap-utilities/
)
target_link_libraries(wifiap-binding PRIVATE ${deps_LIBRARIES})
target_link_libraries(wifiap-binding PRIVATE wifiap-utilities)
configure_file(manifest.yml.in ${CMAKE_BINARY_DIR}/manifest.yml @ONLY)

# Install wifiap-binding
install(TARGETS wifiap-binding DESTINATION ${APP_DIR}/lib)
install(FILES ${CMAKE_BINARY_DIR}/manifest.yml DESTINATION ${APP_DIR}/.rpconfig)
install(FILES conf.d/project/etc/wifi-wifiap-binding-default-config.json DESTINATION ${APP_DIR}/etc)
install(FILES scripts/wifi_setup.sh DESTINATION ${APP_DIR}/var/ PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ)
