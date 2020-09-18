--[[
    Copyright (C) 2019 "IoT.bzh"
    Author  Salma Raiss <asalma.raiss@iot.bzh>

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.


    NOTE: strict mode: every global variables should be prefixed by '_'
--]]

local testPrefix ="rp_wifiap_BasicTests_"
local api="wifiAp"

_AFT.setBeforeEach(function() print("~~~~~ Begin Test ~~~~~") end)
_AFT.setAfterEach(function() print("~~~~~ End Test ~~~~~") end)

_AFT.setBeforeAll(function() print("~~~~~~~~~~ BEGIN BASIC TESTS ~~~~~~~~~~") return 0 end)
_AFT.setAfterAll(function() print("~~~~~~~~~~ END BASIC TESTS ~~~~~~~~~~") return 0 end)

-- This tests the "setSsid" verb of the wifiAp API
_AFT.testVerbStatusSuccess(testPrefix.."setSsid",api,"setSsid", "testAp",nil,nil)

-- This tests the "setPassPhrase" verb of the wifiAp API
_AFT.testVerbStatusSuccess(testPrefix.."setPassPhrase",api,"setPassPhrase", "12345678",nil,nil)

-- This tests the "setDiscoverable" verb of the wifiAp API
_AFT.testVerbStatusSuccess(testPrefix.."setDiscoverable",api,"setDiscoverable", true,nil,nil)

-- This tests the "setIeeeStandard" verb of the wifiAp API
_AFT.testVerbStatusSuccess(testPrefix.."setIeeeStandard",api,"setIeeeStandard", "4",nil,nil)

-- This tests the "setIpRange" verb of the wifiAp API
_AFT.testVerbStatusSuccess(testPrefix.."setIpRange",api,"setIpRange", {ip_ap = "192.168.5.1", ip_start = "192.168.5.10",
ip_stop = "192.168.5.100", ip_netmask = "255.255.255.0" },
nil,
nil)

-- This tests the "setChannel" verb of the wifiAp API
_AFT.testVerbStatusSuccess(testPrefix.."setChannel",api,"setChannel", "4",nil,nil)

-- This tests the "getIeeeStandard" verb of the wifiAp API
_AFT.testVerbStatusSuccess(testPrefix.."getIeeeStandard",api,"getIeeeStandard", {},nil,nil)

-- This tests the "setSecurityProtocol" verb of the wifiAp API
_AFT.testVerbStatusSuccess(testPrefix.."setSecurityProtocol",api,"setSecurityProtocol", "WPA2",nil,nil)

-- This tests the "setPreSharedKey" verb of the wifiAp API
_AFT.testVerbStatusSuccess(testPrefix.."setPreSharedKey",api,"setPreSharedKey", "12345678",nil,nil)

-- This tests the "setCountryCode" verb of the wifiAp API
_AFT.testVerbStatusSuccess(testPrefix.."setCountryCode",api,"setCountryCode", "FR",nil,nil)

-- This tests the "SetMaxNumberClients" verb of the wifiAp API
_AFT.testVerbStatusSuccess(testPrefix.."SetMaxNumberClients",api,"SetMaxNumberClients", "10",nil,nil)

-- This tests the "start" verb of the wifiAp API
_AFT.testVerbStatusSuccess(testPrefix.."start",api,"start", {},
function()
  AFT.callVerb(api,"setSsid","testAp")
  AFT.callVerb(api,"setChannel","6")
  AFT.callVerb(api,"setSecurityProtocol","WPA2")
  AFT.callVerb(api,"setIpRange",{ip_ap = "192.168.4.1", ip_start = "192.168.4.10",
  ip_stop = "192.168.4.100", ip_netmask = "255.255.255.0" })
end,
function()
  AFT.callVerb(api,"stop",nil)
end)

-- This tests the "stop" verb of the wifiAp API
_AFT.testVerbStatusSuccess(testPrefix.."stop",api,"stop", {},
function()
  AFT.callVerb(api,"setSsid","testAp")
  AFT.callVerb(api,"setChannel","6")
  AFT.callVerb(api,"setSecurityProtocol","WPA2")
  AFT.callVerb(api,"setIpRange",{ip_ap = "192.168.4.1", ip_start = "192.168.4.10",
  ip_stop = "192.168.4.100", ip_netmask = "255.255.255.0" })
  AFT.callVerb(api,"start",nil)
end,
nil)

-- This tests 'wrong_verb'
_AFT.testVerbStatusError(testPrefix.."wrong_verb",api,"error",{}, nil, nil)

------------------------------------------------------------------------------------------------------------------------

-- This tests 'setSsid without argument'
_AFT.testVerbStatusError(testPrefix.."set_ssid_without_argument",api,"setSsid",{}, nil, nil)

-- This tests 'setSsid with a bad argument'
_AFT.testVerbStatusError(testPrefix.."set_ssid_with_bad_argument",api,"setSsid","test wifi access point with bad arg",
nil,
nil)

------------------------------------------------------------------------------------------------------------------------

-- This tests 'setIeeeStandard without argument'
_AFT.testVerbStatusError(testPrefix.."set_IeeeStandard_without_argument",api,"setIeeeStandard",{}, nil, nil)

-- This tests 'setIeeeStandard IEEE Standard bit mask value corresponds to no hardware mode'
_AFT.testVerbStatusError(testPrefix.."set_ieee_standard_value_with_no_hw_mode",api,"setIeeeStandard",128, nil, nil)

-- This tests 'setIeeeStandard with more then one hardware mode argument'
_AFT.testVerbStatusError(testPrefix.."set_IeeeStandard_with_more_the_one_hardware_argument",api,"setIeeeStandard",3,
nil,
nil)

-- This tests 'setIeeeStandard with invalid argument'
_AFT.testVerbStatusError(testPrefix.."set_IeeeStandard_with_invalid_argument",api,"setIeeeStandard",21, nil, nil)

------------------------------------------------------------------------------------------------------------------------

-- This tests 'setDiscoverable without argument'
_AFT.testVerbStatusError(testPrefix.."set_discoverability_without_argument",api,"setSsid",{}, nil, nil)

------------------------------------------------------------------------------------------------------------------------

-- This tests 'setPassphrase without argument'
_AFT.testVerbStatusError(testPrefix.."set_passphrase_without_argument",api,"setPassphrase",{}, nil, nil)

-- This tests 'setPassphrase with invalid length argument'
_AFT.testVerbStatusError(testPrefix.."set_passphrase_with_invalid_length_argument",api,"setPassphrase",1234, nil, nil)

------------------------------------------------------------------------------------------------------------------------

-- This tests 'setChannel without argument'
_AFT.testVerbStatusError(testPrefix.."set_channel_without_argument",api,"setChannel",{}, nil, nil)

-- This tests 'setChannel with invalid argument'
_AFT.testVerbStatusError(testPrefix.."set_channel_invalid_argument",api,"setChannel", 4,
function()
  _AFT.callVerb(api,"setIeeeStandard",129)
end,
nil)

------------------------------------------------------------------------------------------------------------------------

-- This tests 'setSecurityProtocol without argument'
_AFT.testVerbStatusError(testPrefix.."set_security_protocol_without_argument",api,"setSecurityProtocol",{}, nil, nil)

-- This tests 'setSecurityProtocol with invalid argument'
_AFT.testVerbStatusError(testPrefix.."set_security_with_invalid_argument",api,"setSecurityProtocol","WPA", nil, nil)

------------------------------------------------------------------------------------------------------------------------

-- This tests 'setPreSharedKey without argument'
_AFT.testVerbStatusError(testPrefix.."set_pre_shared_key_without_argument",api,"setPreSharedKey",{}, nil, nil)

-- This tests 'setPreSharedKey with invalid length argument'
_AFT.testVerbStatusError(testPrefix.."set_pre_shared_key_with_invalid_length_argument",api,"setPreSharedKey",
"azertyuiopqsdfghjklmwxcvbn1234567890nbvcxwmlkjhgfdsqpoiuytreza0987654321",
nil,
nil)

------------------------------------------------------------------------------------------------------------------------

-- This tests 'setIpRange without argument'
_AFT.testVerbStatusError(testPrefix.."set_ip_range_without_argument",api,"setIpRange",{}, nil, nil)


-- This tests 'setIpRange without argument'
-- _AFT.testVerbStatusError(testPrefix.."setIpRange",api,"setIpRange", {ip_ap = "192.168.5.1", ip_start = "192.168.5.10",
-- ip_stop = "192.168.5.100", ip_netmask = "255.255.255.0" },
-- nil,
-- nil)

------------------------------------------------------------------------------------------------------------------------

-- This tests 'setCountryCode without argument'
_AFT.testVerbStatusError(testPrefix.."set_country_code_without_argument",api,"setCountryCode",{}, nil, nil)

-- This tests 'setCountryCode with invalid argument'
_AFT.testVerbStatusError(testPrefix.."set_country_code_with_invalid_argument",api,"setCountryCode","WPA", nil, nil)

------------------------------------------------------------------------------------------------------------------------

-- This tests 'SetMaxNumberClients without argument'
_AFT.testVerbStatusError(testPrefix.."set_max_number_clients_without_argument",api,"SetMaxNumberClients",{}, nil, nil)

-- This tests 'SetMaxNumberClients with out of range argument'
_AFT.testVerbStatusError(testPrefix.."set_max_number_clients_with_out_of_range_argument",api,"SetMaxNumberClients",11,
nil,
nil)

------------------------------------------------------------------------------------------------------------------------

_AFT.exitAtEnd()
