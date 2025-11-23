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
  for PARTITION in "1" "3" "5" "6" "7"; do
    if [ ! -b /dev/${1}${PARTITION} ]; then
      return 1
    fi
  done

  BOOT_PARTITION_SIZE=134217728
  SYSTEM_PARTITION_SIZE=3221225472
  VENDOR_PARTITION_SIZE=402653184
  METADATA_PARTITION_SIZE=16777216

  PARTITION1=$(lsblk -o LABEL,SIZE -b /dev/${1}1 | tail -n 1)
  PARTITION3=$(lsblk -o LABEL,SIZE -b /dev/${1}3 | tail -n 1)
  PARTITION5=$(lsblk -o LABEL,SIZE -b /dev/${1}5 | tail -n 1)
  PARTITION6=$(lsblk -o LABEL,SIZE -b /dev/${1}6 | tail -n 1)
  PARTITION7=$(lsblk -o LABEL,SIZE -b /dev/${1}7 | tail -n 1)

  if [ $(echo ${PARTITION1} | awk {'print $1'}) != "boot" ] || [ $(echo ${PARTITION1} | awk {'print $2'}) != ${BOOT_PARTITION_SIZE} ]; then
    return 1
  fi
  if [ $(echo ${PARTITION5} | awk {'print $1'}) != "/" ] || [ $(echo ${PARTITION5} | awk {'print $2'}) != ${SYSTEM_PARTITION_SIZE} ]; then
    return 1
  fi
  if [ $(echo ${PARTITION6} | awk {'print $1'}) != "vendor" ] || [ $(echo ${PARTITION6} | awk {'print $2'}) != ${VENDOR_PARTITION_SIZE} ]; then
    return 1
  fi
  if [ $(echo ${PARTITION7} | awk {'print $1'}) != "metadata" ] || [ $(echo ${PARTITION7} | awk {'print $2'}) != ${METADATA_PARTITION_SIZE} ]; then
    return 1
  fi
  if [ $(echo ${PARTITION3} | awk {'print $1'}) != "userdata" ]; then
    return 1
  fi

  DEVICE=${1}
  return 0
}

find_device() {
  for SDX in "sda" "sdb" "sdc" "sdd" "sde" "sdf"; do
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
    echo "Wiping metadata and userdata partitions..."
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
  echo "Creating metadata..."
  sudo umount /dev/${DEVICE}7
  sudo wipefs -a /dev/${DEVICE}7
  sudo mkfs.ext4 /dev/${DEVICE}7 -I 512 -L metadata

  echo "Creating userdata..."
  sudo umount /dev/${DEVICE}3
  sudo wipefs -a /dev/${DEVICE}3
  sudo mkfs.ext4 /dev/${DEVICE}3 -I 512 -L userdata
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
  write_partition system 5
  write_partition vendor 6
  finish
elif [ ! -z $1 ] && [ $1 == "boot" ]; then
  find_device
  confirm ${TARGET} "boot"
  write_partition boot 1
  finish
elif [ ! -z $1 ] && [ $1 == "system" ]; then
  find_device
  confirm ${TARGET} "system"
  write_partition system 5
  finish
elif [ ! -z $1 ] && [ $1 == "vendor" ]; then
  find_device
  confirm ${TARGET} "vendor"
  write_partition vendor 6
  finish
elif [ ! -z $1 ] && [ $1 == "wipe" ]; then
  find_device
  confirm ${TARGET} "wipe"
  wipe_userdata
  finish
else
  exit_with_error "Usage: $0 [boot|system|vendor|wipe]"
fi
