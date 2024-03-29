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
IFACE=$2
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

#IFACE=$(diff <(ip -br -c l) /tmp/listinterfaces.txt | grep "wlan" | tr -s ' ' | cut -d ' ' -f2)

#echo "${CMD}"
case ${CMD} in
    GET_VIRTUAL_INTERFACE_NAME)
      diff=$(diff /tmp/newlist.txt /tmp/listinterfaces.txt)
      viface=$(echo $diff | grep "wlan" | tr -s ' ' | cut -d ' ' -f3)
      echo $viface && exit ${SUCCESS}
      ;;
    WIFI_START)
        retries=10
        for i in $(seq 1 ${retries})
        do
            echo "loop=${i}"
            sleep 1
            [ -e /sys/class/net/${IFACE} ] && break
        done
        if [ "${i}" -ne "${retries}" ]; then
            sudo ip link set ${IFACE} up
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
    && wpa_cli -i${IFACE} terminate
    ;;

  WIFI_SET_EVENT)
    (iw event ) || exit ${ERROR}
    ;;

  WIFI_UNSET_EVENT)
    count=$(/usr/bin/pgrep -c iw)
    [ "${count}" -eq 0 ] && exit ${SUCCESS}
    for i in $(seq 1 "${count}")
    do
        pid=$(/usr/bin/pgrep -n iw)
        sudo kill -9 "${pid}"
    done
    count=$(/usr/bin/pgrep -c iw)
    [ "${count}" -eq 1 ]|| exit ${ERROR}
    ;;

  WIFI_CHECK_HWSTATUS)
    #Client request disconnection if interface in up
    sudo ip link show | grep ${IFACE} > /dev/null 2>&1
    [ $? -eq 0 ] && exit ${SUCCESS}
    sleep 1
    #Check WiFi stop called or not
    sudo lsmod | grep ${LBWIFIMOD} > /dev/null 2>&1
    #Driver stays, hardware removed
    [ $? -eq 0 ] && exit ${HARDWAREABSENCE}
    #WiFi stop called
    exit ${NODRIVER} ;;

  WIFIAP_HOSTAPD_START)
    (sudo hostapd /tmp/hostapd.conf -i ${IFACE} -B) && exit ${SUCCESS}
    exit ${ERROR} ;;

  WIFIAP_HOSTAPD_STOP)
    if test -f "/tmp/dnsmasq.wlan.conf"; then
      rm -f /tmp/dnsmasq.wlan.conf
    fi
    if test -f "/tmp/add_hosts"; then
      rm -f /tmp/add_hosts
    fi
    sudo killall hostapd
    sleep 1;
    rm -f /tmp/hostapd.conf
    pidof hostapd && (sudo kill -9 "$(pidof hostapd)" || exit ${ERROR})
    pidOfDnsmasq=$(pgrep -a dnsmasq | grep "/tmp/dnsmasq.wlan.conf" | tr -s ' ' | cut -d ' ' -f1)
    if [ -n "$pidOfDnsmasq" ]; then
      sudo kill -9 ${pidOfDnsmasq} || exit ${ERROR}
    fi
    ;;

  WIFIAP_WLAN_UP)
    AP_IP=$3
    sudo ip -br l | grep ${IFACE} || exit ${ERROR}
    sudo ip addr flush dev ${IFACE} || exit ${ERROR}
    sudo ip addr add ${AP_IP} dev ${IFACE} || exit ${ERROR}
    sudo ip link set ${IFACE} up || exit ${ERROR}
    ;;

  DNSMASQ_RESTART)
    AP_IP=$3
    sudo ip addr flush dev ${IFACE} || exit ${ERROR}
    sudo ip addr add ${AP_IP} dev ${IFACE} || exit ${ERROR}
    sudo ip link set ${IFACE} up || exit ${ERROR}
    echo "interface=${IFACE}" >> /tmp/dnsmasq.wlan.conf
    sudo dnsmasq -C /tmp/dnsmasq.wlan.conf|| exit ${ERROR}
    ;;

  *)
    echo "Parameter not valid"
    exit ${ERROR} ;;
esac
echo "DONE"
exit ${SUCCESS}

