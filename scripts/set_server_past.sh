#!/bin/bash
while true; do
  sudo timedatectl set-ntp false
  sudo timedatectl set-time 2019-04-18
  echo $(timedatectl status)
  sleep 60
done

