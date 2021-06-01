#!/bin/bash

#Running this script as root may be a problem while launching afm-test command
if [ "$EUID" -eq 0 ]
then
       echo "Please do not run this script as root !"
       exit
fi


DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

function start_virtual_interface {
        sudo ip -br l >/tmp/listinterfaces.txt
        sudo modprobe mac80211_hwsim radios=1
        sudo ip -br l >/tmp/newlist.txt
}
function stop_virtual_interface {
        sudo rmmod mac80211_hwsim
        sudo rm /tmp/listinterfaces.txt /tmp/newlist.txt
}

echo "--- Launching virtual interface instance ---"
start_virtual_interface

echo "--- Launching wifi access point binding tests ---"
afm-test package package-test/ -t 10 -c

echo "--- Killing created virtual interface ---"
stop_virtual_interface

