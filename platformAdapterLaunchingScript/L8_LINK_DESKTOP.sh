#!/bin/sh
# ($1:) -d Debug logs
# $1: Command (ex:  WIFI_START
#                   WIFICLIENT_CONNECT
# $2: wpa_supplicant.conf file directory

if [ "$1" = "-d" ]; then
    shift
    set -x
fi

CMD=$1
# WiFi interface
IFACE=wlxec3dfde591cd
# If WLAN interface does not exist but driver is installed, means WiFi hardware is absent
HARDWAREABSENCE=50
# LB wifi module name
LBWIFIMOD=rtl8192cu
# If wpa_supplicant is running already
WPADUPLICATE=14
# WiFi driver is not installed
NODRIVER=100
TIMEOUT=8
SUCCESS=0
ERROR=127

# Check the connection on the WiFi network interface.
# Exit with 0 if connected otherwise exit with 8 (time out)
CheckConnection()
{
    retries=10
    echo "Checking connection..."
    # Verify connection status
    for i in $(seq 1 ${retries})
    do
        echo "loop=${i}"
        (iw ${IFACE} link | grep "Connected to") && break
        sleep 1
    done
    if [ "${i}" -eq "${retries}" ]; then
        # Connection request time out.
        exit ${TIMEOUT}
    fi
    # Connected.
    exit ${SUCCESS}

}
echo "${CMD}"
case ${CMD} in
    WIFI_START)
        retries=10
        for i in $(seq 1 ${retries})
        do
            echo "loop=${i}"
            sleep 1
            sudo [ -e /sys/class/net/${IFACE} ] && break
        done
        if [ "${i}" -ne "${retries}" ]; then
            sudo ifconfig ${IFACE} up
            exit ${SUCCESS}
        fi
        moduleString=$(sudo lsmod | grep ${LBWIFIMOD}) > /dev/null
        if [ -n "${moduleString}" ]; then
            ret=${HARDWAREABSENCE}
        else
            ret=${ERROR}
        fi
        # Do clean up

        exit ${ret} ;;

  WIFI_STOP)
    # If wpa_supplicant is still running, terminate it
    (ps -ax | grep wpa_supplicant | grep ${IFACE} >/dev/null 2>&1) \
    && wpa_cli -i${IFACE} terminate
    ;;

  WIFI_SET_EVENT)
    iw event || exit ${ERROR}
    ;;

  WIFI_UNSET_EVENT)
    count=$(pgrep -c iw)
    [ "${count}" -eq 0 ] && exit ${SUCCESS}
    for i in $(seq 1 "${count}")
    do
        pid=$(pgrep -n iw)
        /bin/kill -9 "${pid}"
    done
    count=$(pgrep -c iw)
    [ "${count}" -eq 0 ] || exit ${ERROR}
    ;;

  WIFI_CHECK_HWSTATUS)
    #Client request disconnection if interface in up
    ifconfig | grep ${IFACE} > /dev/null 2>&1
    [ $? -eq 0 ] && exit ${SUCCESS}
    sleep 1
    #Check WiFi stop called or not
    lsmod | grep ${LBWIFIMOD} > /dev/null 2>&1
    #Driver stays, hardware removed
    [ $? -eq 0 ] && exit ${HARDWAREABSENCE}
    #WiFi stop called
    exit ${NODRIVER} ;;

  WIFIAP_HOSTAPD_START)
    (hostapd /tmp/hostapd.conf ) && exit ${SUCCESS}
    exit ${ERROR} ;;

  WIFIAP_HOSTAPD_STOP)
    rm -f /tmp/dhcp.wlan.conf
    unlink /etc/dhcp/dhcpd.conf
    systemctl stop dhcpd.service
    killall hostapd
    sleep 1;
    pidof hostapd && (kill -9 "$(pidof hostapd)" || exit ${ERROR})
    pidof dnsmasq && (kill -9 "$(pidof dnsmasq)" || exit ${ERROR})
    ;;

  WIFIAP_WLAN_UP)
    AP_IP=$2
    sudo ifconfig | grep ${IFACE} || exit ${ERROR}
    sudo ifconfig ${IFACE} "${AP_IP}" up || exit ${ERROR}
    ;;

  DNSMASQ_RESTART)
    echo "interface=${IFACE}" >> /tmp/dnsmasq.wlan.conf
    /etc/init.d/dnsmasq stop
    pidof dnsmasq && (kill -9 `pidof dnsmasq` || exit 127)
    /etc/init.d/dnsmasq start || exit ${ERROR}
    ;;

  DHCP_CLIENT_RESTART)
    AP_IP=$2
    sudo ifconfig ${IFACE} up ${AP_IP} netmask 255.255.255.0
    sudo systemctl restart isc-dhcp-server.service
    ;;


  WIFICLIENT_START_SCAN)
    (iw dev ${IFACE} scan | grep 'BSS\|SSID\|signal') || exit ${ERROR}
    ;;

  WIFICLIENT_CONNECT)
    WPA_CFG=$2
    [ -f "${WPA_CFG}" ] || exit ${ERROR}
    # wpa_supplicant is running, return duplicated request
    ps -A | grep wpa_supplicant && exit ${WPADUPLICATE}
    wpa_supplicant -d -Dnl80211 -c "${WPA_CFG}" -i${IFACE} -B || exit ${ERROR}
    CheckConnection ;;

  WIFICLIENT_DISCONNECT)
    wpa_cli -i${IFACE} terminate || exit ${ERROR}
    echo "WiFi client disconnected."
    ;;

  IPTABLE_DHCP_INSERT)
    sudo iptables -I INPUT -i ${IFACE} -p udp -m udp \
     --sport 67:68 --dport 67:68 -j ACCEPT  || exit ${ERROR}
    ;;

  IPTABLE_DHCP_DELETE)
    sudo iptables -D INPUT -i ${IFACE} -p udp -m udp \
     --sport 67:68 --dport 67:68 -j ACCEPT  || exit ${ERROR}
    ;;

  *)
    echo "Parameter not valid"
    exit ${ERROR} ;;
esac
exit ${SUCCESS}

