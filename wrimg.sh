#!/bin/bash

#
# Copyright (C) 2025 KonstaKANG
#
# SPDX-License-Identifier: Apache-2.0
#

exit_with_error() {
  echo $@
  exit 1
}

check_device() {
  for PARTITION in "1" "2" "3" "4"; do
    if [ ! -b /dev/${1}${PARTITION} ]; then
      return 1
    fi
  done

  PARTITION1=$(lsblk -o LABEL,SIZE -b /dev/${1}1 | tail -n 1)
  PARTITION2=$(lsblk -o LABEL,SIZE -b /dev/${1}2 | tail -n 1)
  PARTITION3=$(lsblk -o LABEL,SIZE -b /dev/${1}3 | tail -n 1)
  PARTITION4=$(lsblk -o LABEL,SIZE -b /dev/${1}4 | tail -n 1)

  if [ $(echo ${PARTITION1} | awk {'print $1'}) != "boot" ] || [ $(echo ${PARTITION1} | awk {'print $2'}) != "134217728" ]; then
    return 1
  fi
  if [ $(echo ${PARTITION2} | awk {'print $1'}) != "/" ] || [ $(echo ${PARTITION2} | awk {'print $2'}) != "2147483648" ]; then
    return 1
  fi
  if [ $(echo ${PARTITION3} | awk {'print $1'}) != "vendor" ] || [ $(echo ${PARTITION3} | awk {'print $2'}) != "268435456" ]; then
    return 1
  fi
  if [ $(echo ${PARTITION4} | awk {'print $1'}) != "userdata" ]; then
    return 1
  fi

  DEVICE=${1}
  return 0
}

find_device() {
  for SDX in "sda" "sdb" "sdc" "sdd"; do
    check_device ${SDX}
    if [ $? == "0" ]; then
      break
    fi
  done

  if [ -z ${DEVICE} ]; then
    exit_with_error "Unable to find suitable block device!"
  fi
}

confirm() {
  echo "Build target ${1}..."
  if [ "${2}" == "wipe" ]; then
    echo "Wiping userdata partition..."
  else
    echo "Writing ${2} image..."
  fi
  echo "Writing to device /dev/${DEVICE}..."
  lsblk -o NAME,LABEL,SIZE /dev/${DEVICE}

  read -p "Continue (y/n)? " -n 1 -r RESPONSE
  echo ""
  if [[ ! ${RESPONSE} =~ ^[Yy]$ ]]; then
    exit_with_error "Exiting!"
  fi
}

write_partition() {
  if [ ! -f ${ANDROID_PRODUCT_OUT}/${1}.img ]; then
    exit_with_error "Partition image not found. Run 'make ${1}image' first."
  fi

  echo "Copying ${1}..."
  sudo umount /dev/${DEVICE}${2}
  sudo dd if=${ANDROID_PRODUCT_OUT}/${1}.img of=/dev/${DEVICE}${2} bs=1M
}

wipe_userdata() {
  echo "Creating userdata..."
  sudo umount /dev/${DEVICE}4
  sudo wipefs -a /dev/${DEVICE}4
  sudo mkfs.ext4 /dev/${DEVICE}4 -I 512 -L userdata
}

finish() {
  sync
  echo "Done!"
  exit 0
}

if [ -z ${TARGET_PRODUCT} ]; then
  exit_with_error "TARGET_PRODUCT environment variable is not set. Run lunch first."
fi

if [ -z ${ANDROID_PRODUCT_OUT} ]; then
  exit_with_error "ANDROID_PRODUCT_OUT environment variable is not set. Run lunch first."
fi

TARGET=$(echo ${TARGET_PRODUCT} | sed 's/^aosp_//')
DEVICE=

if [ -z $1 ]; then
  find_device
  confirm ${TARGET} "boot, system, and vendor"
  write_partition boot 1
  write_partition system 2
  write_partition vendor 3
  finish
elif [ ! -z $1 ] && [ $1 == "boot" ]; then
  find_device
  confirm ${TARGET} "boot"
  write_partition boot 1
  finish
elif [ ! -z $1 ] && [ $1 == "system" ]; then
  find_device
  confirm ${TARGET} "system"
  write_partition system 2
  finish
elif [ ! -z $1 ] && [ $1 == "vendor" ]; then
  find_device
  confirm ${TARGET} "vendor"
  write_partition vendor 3
  finish
elif [ ! -z $1 ] && [ $1 == "wipe" ]; then
  find_device
  confirm ${TARGET} "wipe"
  wipe_userdata
  finish
else
  exit_with_error "Usage: $0 [boot|system|vendor|wipe]"
fi
