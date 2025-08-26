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
        r = libafb.callsync_ex(self.binder, "wifiAp", "setSsid", "testAP")
        assert r.status == 0

    def test_set_channel(self):
        """Test setting the channel"""
        r = libafb.callsync_ex(self.binder, "wifiAp", "setChannel", 1)
        assert r.status == 0

    def test_start_stop_ap(self):
        """Test starting and stopping the access point"""
        # Start AP
        r = libafb.callsync_ex(self.binder, "wifiAp", "start")
        assert r.status == 0
        sleep(2)
        
        # Stop AP
        r = libafb.callsync_ex(self.binder, "wifiAp", "stop")
        assert r.status == 0

    def test_set_interface_name(self):
        """Test setting interface name"""
        r = libafb.callsync_ex(self.binder, "wifiAp", "setInterfaceName", "wlan1")
        assert r.status == 0

    def test_set_host_name(self):
        """Test setting host name"""
        r = libafb.callsync_ex(self.binder, "wifiAp", "setHostName", "localhost")
        assert r.status == 0

    def test_set_domain_name(self):
        """Test setting domain name"""
        r = libafb.callsync_ex(self.binder, "wifiAp", "setDomainName", "FR")
        assert r.status == 0

    def test_set_pass_phrase(self):
        """Test setting passphrase"""
        r = libafb.callsync_ex(self.binder, "wifiAp", "setPassPhrase", "passwordtest")
        assert r.status == 0

    def test_set_discoverable(self):
        """Test setting discoverable flag"""
        # Test with true
        r = libafb.callsync_ex(self.binder, "wifiAp", "setDiscoverable", True)
        assert r.status == 0
        
        # Test with false
        r = libafb.callsync_ex(self.binder, "wifiAp", "setDiscoverable", False)
        assert r.status == 0

    def test_set_ieee_standard(self):
        """Test setting IEEE standard"""
        r = libafb.callsync_ex(self.binder, "wifiAp", "setIeeeStandard", 2)
        assert r.status == 0

    def test_get_ieee_standard(self):
        """Test getting IEEE standard"""
        r = libafb.callsync_ex(self.binder, "wifiAp", "getIeeeStandard", 1)
        assert r.status == 0

    def test_set_country_code(self):
        """Test setting country code"""
        r = libafb.callsync_ex(self.binder, "wifiAp", "setCountryCode", "FR")
        assert r.status == 0

    def test_set_max_number_clients(self):
        """Test setting max number of clients"""
        r = libafb.callsync_ex(self.binder, "wifiAp", "SetMaxNumberClients", 4)
        assert r.status == 0

    def test_get_ap_clients_number(self):
        """Test getting current number of clients"""
        r = libafb.callsync_ex(self.binder, "wifiAp", "getAPclientsNumber")
        assert r.status == 0

if __name__ == "__main__":
    run_afb_binding_tests(bindings)