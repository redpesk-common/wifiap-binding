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
IFACE=wlan0
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

echo "${CMD}"
case ${CMD} in
    WIFI_START)
        retries=10
        for i in $(seq 1 ${retries})
        do
            echo "loop=${i}"
            sleep 1
            [ -e /sys/class/net/${IFACE} ] && break
        done
        if [ "${i}" -ne "${retries}" ]; then
            /sbin/ifconfig ${IFACE} up
            exit ${SUCCESS}
        fi
        moduleString=$(/sbin/lsmod | grep ${LBWIFIMOD}) > /dev/null
        if [ -n "${moduleString}" ]; then
            ret=${HARDWAREABSENCE}
        else
            ret=${ERROR}
        fi
        # Do clean up

        exit ${ret} ;;

  WIFI_STOP)
    # If wpa_supplicant is still running, terminate it
    (/bin/ps -ax | grep wpa_supplicant | grep ${IFACE} >/dev/null 2>&1) \
    && /sbin/wpa_cli -i${IFACE} terminate
    ;;

  WIFI_SET_EVENT)
    /usr/sbin/iw event || exit ${ERROR}
    ;;

  WIFI_UNSET_EVENT)
    count=$(/usr/bin/pgrep -c iw)
    [ "${count}" -eq 0 ] && exit ${SUCCESS}
    for i in $(seq 1 "${count}")
    do
        pid=$(/usr/bin/pgrep -n iw)
        /bin/kill -9 "${pid}"
    done
    count=$(/usr/bin/pgrep -c iw)
    [ "${count}" -eq 0 ] || exit ${ERROR}
    ;;

  WIFI_CHECK_HWSTATUS)
    #Client request disconnection if interface in up
    /sbin/ifconfig | grep ${IFACE} > /dev/null 2>&1
    [ $? -eq 0 ] && exit ${SUCCESS}
    sleep 1
    #Check WiFi stop called or not
    /sbin/lsmod | grep ${LBWIFIMOD} > /dev/null 2>&1
    #Driver stays, hardware removed
    [ $? -eq 0 ] && exit ${HARDWAREABSENCE}
    #WiFi stop called
    exit ${NODRIVER} ;;

  WIFIAP_HOSTAPD_START)
    (/bin/hostapd /tmp/hostapd.conf -i ${IFACE} -B) && exit ${SUCCESS}
    exit ${ERROR} ;;

  WIFIAP_HOSTAPD_STOP)
    rm -f /tmp/dhcp.wlan.conf
    /usr/bin/unlink /etc/dhcp/dhcpd.conf
    systemctl stop dhcpd.service
    killall hostapd
    sleep 1;
    pidof hostapd && (kill -9 "$(pidof hostapd)" || exit ${ERROR})
    pidof dnsmasq && (kill -9 "$(pidof dnsmasq)" || exit ${ERROR})
    /etc/init.d/dnsmasq start || exit ${ERROR}
    ;;

  WIFIAP_WLAN_UP)
    AP_IP=$2
    /sbin/ifconfig | grep ${IFACE} || exit ${ERROR}
    /sbin/ifconfig ${IFACE} "${AP_IP}" up || exit ${ERROR}
    ;;

  DHCP_CLIENT_RESTART)
    AP_IP=$2
    ifconfig ${IFACE} up ${AP_IP} netmask 255.255.255.0
    systemctl restart dhcpd.service
    ;;

  IPTABLE_DHCP_INSERT)
    /usr/sbin/iptables -I INPUT -i ${IFACE} -p udp -m udp \
     --sport 67:68 --dport 67:68 -j ACCEPT  || exit ${ERROR}
    ;;

  IPTABLE_DHCP_DELETE)
    /usr/sbin/iptables -D INPUT -i ${IFACE} -p udp -m udp \
     --sport 67:68 --dport 67:68 -j ACCEPT  || exit ${ERROR}
    ;;

  *)
    echo "Parameter not valid"
    exit ${ERROR} ;;
esac
exit ${SUCCESS}

