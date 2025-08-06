from afb_test import AFBTestCase, configure_afb_binding_tests, run_afb_binding_tests

import libafb

import pdb
import time
from time import sleep

bindings = {"wifiAp": f"wifiap-binding.so"}


def setUpModule():
    configure_afb_binding_tests(bindings=bindings)


class TestWifiAp(AFBTestCase):
    
    def test_set_ssid(self):
        """Test setting the SSID"""

        # Set SSID
        r = libafb.callsync(self.binder, "wifiAp", "setSsid","testAP")
        assert r.status == 0

    def test_set_channel(self):
        """Test setting the channel"""
        
        # Set channel
        r = libafb.callsync(self.binder, "wifiAp", "setChannel",1)
        assert r.status == 0

    
    # def test_set_security_protocol(self):
    #     """
    #         Test setting the security protocol
            
    #         Note proléme : 
    #         - Normalement on passerait juste "WPA2" comme paramètre
    #         - Mais le wrapper Python transforme ça en "\"WPA2\"" (JSON string)
    #         - Le binding C attend juste "WPA2" sans guillemets
    #     """
    #     r = libafb.callsync(self.binder, "wifiAp", "setSecurityProtocol", "WPA2")
    #     assert r.status == 0


    def test_start_stop_ap(self):
        """Test starting the access point"""
        
        # Start AP
        r = libafb.callsync(self.binder, "wifiAp", "start", None)
        assert r.status == 0

        sleep(2)  # Wait for AP to start
        
        # Stop AP
        r = libafb.callsync(self.binder, "wifiAp", "stop", None)
        assert r.status == 0


if __name__ == "__main__":
    run_afb_binding_tests(bindings)
