###########################################################################
# Copyright 2024 IoT.bzh
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

# Add target to project dependency list
PROJECT_TARGET_ADD(wifiap-binding)
	# Define project Targets
	ADD_LIBRARY(wifiap-binding MODULE wifiAp.c
										  wifiAp.h
	     		)

	# Binder exposes a unique public entry point
	SET_TARGET_PROPERTIES(${TARGET_NAME} PROPERTIES
		PREFIX "afb-"
		LABELS "BINDING"
		OUTPUT_NAME ${TARGET_NAME}
	)
	# Library dependencies (include updates automatically)
	TARGET_LINK_LIBRARIES(${TARGET_NAME}
		wifi-ap-utilities
	)
	option(TEST_MODE "Use wifi_setup_test script to run tests" OFF)
	if (TEST_MODE)
	  add_definitions(-DTEST_MODE=1)
	endif()
