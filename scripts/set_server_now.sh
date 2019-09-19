#!/bin/bash
sudo timedatectl set-ntp true
echo $(timedatectl status)

