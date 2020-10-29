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
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#define AFB_BINDING_VERSION 3
#endif

#include "utilities.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <afb/afb-binding.h>

//--------------------------------------------------------------------------------------------------
/**
 * Returns the number of bytes in the character that starts with a given byte.
 *
 * @return
 *      Number of bytes in the character, or 0 if the byte provided is not a valid starting byte.
 */
//----------------------------------------------------------------------------------------------------------------------
size_t utf8_NumBytesInChar
(
    const char firstByte    ///< [IN] The first byte in the character.
)
{
    if ( (firstByte & 0x80) == 0x00 )
    {
        return 1;
    }
    else if ( (firstByte & 0xE0) == 0xC0 )
    {
        return 2;
    }
    else if ( (firstByte & 0xF0) == 0xE0 )
    {
        return 3;
    }
    else if ( (firstByte & 0xF8) == 0xF0 )
    {
        return 4;
    }
    else
    {
        return 0;
    }
}



/**
 * This function copies the string in srcStr to the start of destStr and returns the number of bytes
 * copied (not including the NULL-terminator) in numBytesPtr.  Null can be passed into numBytesPtr
 * if the number of bytes copied is not needed.  The srcStr must be in UTF-8 format.
 *
 * If the size of srcStr is less than or equal to the destination buffer size then the entire srcStr
 * will be copied including the null-character.  The rest of the destination buffer is not modified.
 *
 * If the size of srcStr is larger than the destination buffer then the maximum number of characters
 * (from srcStr) plus a null-character that will fit in the destination buffer is copied.
 *
 * UTF-8 characters may be more than one byte long and this function will only copy whole characters
 * not partial characters.  Therefore, even if srcStr is larger than the destination buffer the
 * copied characters may not fill the entire destination buffer because the last character copied
 * may not align exactly with the end of the destination buffer.
 *
 * The destination string will always be Null-terminated, unless destSize is zero.
 *
 * If destStr and srcStr overlap the behaviour of this function is undefined.
 *
 * @return
 *      - LE_OK if srcStr was completely copied to the destStr.
 *      - LE_OVERFLOW if srcStr was truncated when it was copied to destStr.
 */
//--------------------------------------------------------------------------------------------------
int utf8_Copy
(
    char* destStr,          ///< [IN] The destination where the srcStr is to be copied.
    const char* srcStr,     ///< [IN] The UTF-8 source string.
    const size_t destSize,  ///< [IN] Size of the destination buffer in bytes.
    size_t* numBytesPtr     ///< [OUT] The number of bytes copied not including the NULL-terminator.
                            ///        This parameter can be set to NULL if the number of bytes
                            ///        copied is not needed.
)
{
    // Check parameters.
    assert(destStr != NULL);
    assert(srcStr != NULL);
    assert(destSize > 0);

    // Go through the string copying one character at a time.
    size_t i = 0;
    while (1)
    {
        if (srcStr[i] == '\0')
        {
            // NULL character found.  Complete the copy and return.
            destStr[i] = '\0';

            if (numBytesPtr)
            {
                *numBytesPtr = i;
            }

            return 0;
        }
        else
        {
            size_t charLength = utf8_NumBytesInChar(srcStr[i]);

            if (charLength == 0)
            {
                // This is an error in the string format.  Zero out the destStr and return.
                destStr[0] = '\0';

                if (numBytesPtr)
                {
                    *numBytesPtr = 0;
                }

                return 0;
            }
            else if (charLength + i >= destSize)
            {
                // This character will not fit in the available space so stop.
                destStr[i] = '\0';

                if (numBytesPtr)
                {
                    *numBytesPtr = i;
                }

                return -1;
            }
            else
            {
                // Copy the character.
                for (; charLength > 0; charLength--)
                {
                    destStr[i] = srcStr[i];
                    i++;
                }
            }
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------
/**
 * Appends srcStr to destStr by copying characters from srcStr to the end of destStr.  The srcStr
 * must be in UTF-8 format.  The number of bytes in the resultant destStr (not including the
 * NULL-terminator) is returned in destStrLenPtr.
 *
 * A null-character is always added to the end of destStr after all srcStr characters have been
 * copied.
 *
 * This function will copy as many characters as possible from srcStr to destStr while ensuring that
 * the resultant string (including the null-character) will fit within the destination buffer.
 *
 * UTF-8 characters may be more than one byte long and this function will only copy whole characters
 * not partial characters.
 *
 * The destination string will always be Null-terminated, unless destSize is zero.
 *
 * If destStr and srcStr overlap the behaviour of this function is undefined.
 *
 * @return
 *      - LE_OK if srcStr was completely copied to the destStr.
 *      - LE_OVERFLOW if srcStr was truncated when it was copied to destStr.
 */
//--------------------------------------------------------------------------------------------------
int utf8_Append
(
    char* destStr,          ///< [IN] The destination string.
    const char* srcStr,     ///< [IN] The UTF-8 source string.
    const size_t destSize,  ///< [IN] Size of the destination buffer in bytes.
    size_t* destStrLenPtr   ///< [OUT] The number of bytes in the resultant destination string (not
                            ///        including the NULL-terminator).  This parameter can be set to
                            ///        NULL if the destination string size is not needed.
)
{
    // Check parameters.
    assert( (destStr != NULL) && (srcStr != NULL) && (destSize > 0) );

    size_t destStrSize = strlen(destStr);
    int result = utf8_Copy(&(destStr[destStrSize]),
                                      srcStr,
                                      destSize - destStrSize,
                                      destStrLenPtr);

    if (destStrLenPtr)
    {
        *destStrLenPtr += destStrSize;
    }

    return result;
}

//----------------------------------------------------------------------------------------------------------------------
/**
 * Check if a file exists.
 *
 * @return
 *      1 if file exists, or 0 if not.
 */
//----------------------------------------------------------------------------------------------------------------------
int checkFileExists(
    const char *fileName
)
{
    /*open file to read*/
    FILE *file;
    if ( (file = fopen(fileName, "r")) != NULL )
    {
        fclose(file);
        return 1;
    }
    return 0;

}

//----------------------------------------------------------------------------------------------------------------------
/**
 * Create access point specific DHCP configuration file.
 *
 * @return
 *      0 if success, or -1 if not.
 */
//----------------------------------------------------------------------------------------------------------------------

int createDhcpConfigFile
(
    const char *ip_subnet , // IP address of the subnet to delete
    const char *ip_netmask,
    const char *ip_ap     ,
    const char *ip_start  ,
    const char *ip_stop
)
{
    const char *configFileName = "/etc/dhcp/dhcpd.conf";
    const char *tmpFileName = "/tmp/dhcpd.wlan.conf";

    FILE *configFile = fopen(configFileName,"r");
    if (!configFile)
        return -1;

    FILE *tmpConfigFile = fopen(tmpFileName, "w");
    if (!tmpConfigFile)
    {
        fclose(configFile);
        return -2;
    }
    int line;
    while( ( line = fgetc(configFile) ) != EOF )
      fputc(line, tmpConfigFile);

    if (tmpConfigFile != NULL)
    {
        //Interface is generated when COMMAND_DHCP_RESTART called
        fprintf(tmpConfigFile, "subnet %s netmask %s {\n",ip_subnet ,ip_netmask);
        fprintf(tmpConfigFile, "option routers %s;\n",ip_ap);
        fprintf(tmpConfigFile, "option subnet-mask %s;\n",ip_netmask);
        fprintf(tmpConfigFile, "option domain-search    \"iotbzh.lan\";\n");
        fprintf(tmpConfigFile, "range %s %s;}\n", ip_start, ip_stop);
    }

    fclose(configFile);
    fclose(tmpConfigFile);
    return 0;

}

//----------------------------------------------------------------------------------------------------------------------
/**
 * Convert Netmask ip address to CIDR annotation.
 *
 * @return
 *      CIDR annotation.
 */
//----------------------------------------------------------------------------------------------------------------------

int toCidr
(
    const char* ipAddress
)
{
    int netmask_cidr;
    int ipbytes[4];

    netmask_cidr=0;
    sscanf(ipAddress, "%d.%d.%d.%d", &ipbytes[0], &ipbytes[1], &ipbytes[2], &ipbytes[3]);

    for (int i=0; i<4; i++)
    {
        switch(ipbytes[i])
        {
            case 0x80:
                netmask_cidr+=1;
            break;

            case 0xC0:
                netmask_cidr+=2;
            break;

            case 0xE0:
                netmask_cidr+=3;
            break;

            case 0xF0:
                netmask_cidr+=4;
            break;

            case 0xF8:
                netmask_cidr+=5;
            break;

            case 0xFC:
                netmask_cidr+=6;
            break;

            case 0xFE:
                netmask_cidr+=7;
            break;

            case 0xFF:
                netmask_cidr+=8;
            break;

            default:
                return netmask_cidr;
            break;
        }
    }
    return netmask_cidr;
}