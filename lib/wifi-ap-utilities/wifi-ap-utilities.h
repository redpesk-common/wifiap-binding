/*
 * Copyright (C) 2016-2018 "IoT.bzh"
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef UTILITIES_HEADER_FILE
#define UTILITIES_HEADER_FILE

//path to Wifi platform adapter shell script
#define WIFI_SCRIPT "var/wifi_setup.sh"
#define PATH_MAX 8192

#include <stdio.h>

int utf8_Copy(char* destStr, const char* srcStr, const size_t destSize, size_t* numBytesPtr);
int utf8_Append(char* destStr, const char* srcStr, const size_t destSize, size_t* destStrLenPtr);
size_t utf8_NumBytesInChar(const char firstByte);
int checkFileExists(const char *fileName);
int createDhcpConfigFile(const char *ip_subnet , const char *ip_netmask, const char *ip_ap, const char *ip_start, const char *ip_stop);
int createDnsmasqConfigFile(const char *ip_ap, const char *ip_start, const char *ip_stop);
int toCidr(const char* ipAddress);
int getScriptPath(afb_api_t apiHandle, char *buffer, size_t size);
int getScriptPath(afb_api_t apiHandle, char *buffer, size_t size);



#endif