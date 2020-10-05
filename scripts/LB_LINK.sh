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
IFACE=wlp0s29f7u1
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

dosudo() { "$@"; }

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
            dosudo ip link set ${IFACE} up
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
    (ps -ax | grep wpa_supplicant | grep ${IFACE} >/dev/null 2>&1) \
    && dosudo wpa_cli -i${IFACE} terminate
    ;;

  WIFI_SET_EVENT)
    iw event || exit ${ERROR}
    ;;

  WIFI_UNSET_EVENT)
    count=$(/usr/bin/pgrep -c iw)
    [ "${count}" -eq 0 ] && exit ${SUCCESS}
    for i in $(seq 1 "${count}")
    do
        pid=$(/usr/bin/pgrep -n iw)
        dosudo kill -9 "${pid}"
    done
    count=$(/usr/bin/pgrep -c iw)
    [ "${count}" -eq 0 ] || exit ${ERROR}
    ;;

  WIFI_CHECK_HWSTATUS)
    #Client request disconnection if interface in up
    dosudo ip link show | grep ${IFACE} > /dev/null 2>&1
    [ $? -eq 0 ] && exit ${SUCCESS}
    sleep 1
    #Check WiFi stop called or not
    dosudo lsmod | grep ${LBWIFIMOD} > /dev/null 2>&1
    #Driver stays, hardware removed
    [ $? -eq 0 ] && exit ${HARDWAREABSENCE}
    #WiFi stop called
    exit ${NODRIVER} ;;

  WIFIAP_HOSTAPD_START)
    (dosudo hostapd /tmp/hostapd.conf -i ${IFACE} -B) && exit ${SUCCESS}
    exit ${ERROR} ;;

  WIFIAP_HOSTAPD_STOP)
    if test -f "/tmp/dhcp.wlan.conf"; then
      rm -f /tmp/dhcp.wlan.conf
      dosudo unlink /etc/dhcp/dhcpd.conf
    fi
    dosudo systemctl stop dhcpd.service
    dosudo killall hostapd
    sleep 1;
    dosudo rm -f /tmp/hostapd.conf
    pidof hostapd && (dosudo kill -9 "$(pidof hostapd)" || exit ${ERROR})
    ;;

  WIFIAP_WLAN_UP)
    AP_IP=$2
    ip -br l | grep ${IFACE} || exit ${ERROR}
    dosudo ip addr flush dev ${IFACE} || exit ${ERROR}
    dosudo ip addr add ${AP_IP} dev ${IFACE} || exit ${ERROR}
    dosudo ip link set ${IFACE} up || exit ${ERROR}
    ;;

  DHCP_CLIENT_RESTART)
    AP_IP=$2
    dosudo ip addr flush dev ${IFACE} || exit ${ERROR}
    dosudo ip addr add ${AP_IP} dev ${IFACE} || exit ${ERROR}
    dosudo ip link set ${IFACE} up || exit ${ERROR}
    dosudo systemctl restart dhcpd.service
    ;;

  IPTABLE_DHCP_INSERT)
    dosudo iptables -I INPUT -i ${IFACE} -p udp -m udp \
     --sport 67:68 --dport 67:68 -j ACCEPT  || exit ${ERROR}
    ;;

  IPTABLE_DHCP_DELETE)
    dosudo iptables -D INPUT -i ${IFACE} -p udp -m udp \
     --sport 67:68 --dport 67:68 -j ACCEPT  || exit ${ERROR}
    ;;

  *)
    echo "Parameter not valid"
    exit ${ERROR} ;;
esac
echo "DONE"
exit ${SUCCESS}
