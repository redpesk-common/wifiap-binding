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

cmake_minimum_required(VERSION 3.16)

project(wifiap-binding
        VERSION 1.0.1
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

# Compile settings
add_compile_options(
	-Wall -Wextra -Wconversion -Wno-unused-parameter
	-Werror=maybe-uninitialized -Werror=implicit-function-declaration 
	-ffunction-sections -fdata-sections -Wno-shift-count-overflow)

# Check dependencies
pkg_check_modules(deps REQUIRED
    json-c
    afb-binding
    librp-utils-json-c
    liburcu
    afb-helpers4
)

find_program(json2c afb-json2c)

if(NOT json2c)
        message(FATAL_ERROR "afb-json2c not found, please install afb-idl")
endif()

add_custom_target(generate_info_src
        COMMAND afb-json2c info.json > info_generated.c
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src
        COMMENT "Generating source file from JSON"
)

# Compile the library wifiap-utilities
add_library(wifiap-utilities STATIC src/lib/wifi-ap-config.c
                                    src/lib/wifi-ap-data.c
                                    src/lib/wifi-ap-thread.c
                                    src/lib/wifi-ap-utilities.c
)
target_include_directories(wifiap-utilities PRIVATE ${deps_INCLUDE_DIRS})
set_target_properties(wifiap-utilities PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Compile the binding
add_library(wifiap-binding SHARED src/wifiAp.c)
add_dependencies(wifiap-binding generate_info_src)
set_target_properties(wifiap-binding PROPERTIES PREFIX "")
target_include_directories(wifiap-binding PRIVATE ${deps_INCLUDE_DIRS})
target_include_directories(wifiap-binding AFTER PRIVATE lib/wifi-ap-utilities/)
target_link_libraries(wifiap-binding PRIVATE ${deps_LIBRARIES})
target_link_libraries(wifiap-binding PRIVATE wifiap-utilities)
configure_file(manifest.yml.in ${CMAKE_BINARY_DIR}/manifest.yml @ONLY)

set(SCRIPT_INSTALL_DIR ${APP_DIR}/var)
set(WIFI_SCRIPT_MAIN "wifi_setup.sh")
set(WIFI_SCRIPT_TEST "wifi_setup_test.sh")

target_compile_definitions(wifiap-binding PRIVATE
    APP_DIR_="${APP_DIR}"   
)


# Install wifiap-binding
install(TARGETS wifiap-binding DESTINATION ${APP_DIR}/lib)

install(FILES ${CMAKE_BINARY_DIR}/manifest.yml DESTINATION ${APP_DIR}/.rpconfig)

install(FILES config/wifi-wifiap-binding-default-config.json
        DESTINATION ${APP_DIR}/etc
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
        RENAME wifiap-config.json)

install(FILES scripts/${WIFI_SCRIPT_MAIN} 
        DESTINATION ${SCRIPT_INSTALL_DIR} 
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ
        RENAME ${WIFI_SCRIPT_MAIN})

install(FILES scripts/${WIFI_SCRIPT_TEST} 
        DESTINATION ${SCRIPT_INSTALL_DIR} 
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ
        RENAME ${WIFI_SCRIPT_TEST})

install(PROGRAMS ${CMAKE_SOURCE_DIR}/redtest/run-redtest
	DESTINATION /usr/libexec/redtest/${PROJECT_NAME}/)

install(FILES ${CMAKE_SOURCE_DIR}/test/tests.py
    DESTINATION /usr/libexec/redtest/${PROJECT_NAME}/)

