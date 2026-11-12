#!/usr/bin/env bash
# ============================================================================
# vm_config.sh -- Configuration for the VMware guest automation scripts.
#
# Every value can be overridden from the environment, e.g.
#
#     VMX='D:\VMs\Ubuntu.vmx' GUEST_USER=alice GUEST_PASS=secret \
#         ./vmrun.sh check
#
# Copy this file and adjust the defaults for your own machine, or export the
# variables before calling the scripts.
# ============================================================================

# Path to vmrun.exe (VMware Workstation / VMware Player).
VMRUN="${VMRUN:-D:\\VMware\\VMware Workstation\\vmrun.exe}"

# Path to the .vmx file of the Ubuntu virtual machine.
VMX="${VMX:-D:\\Virtualmachine\\Ubuntu 64-bit.vmx}"

# Credentials of the guest user. The guest needs VMware Tools installed so
# that vmrun's guest operations work.
GUEST_USER="${GUEST_USER:-tuananh}"
GUEST_PASS="${GUEST_PASS:-1234}"

# Home directory of the guest user.
GUEST_HOME="${GUEST_HOME:-/home/$GUEST_USER}"

# Directory inside the guest where the project is synchronised to.
GUEST_PROJECT="${GUEST_PROJECT:-$GUEST_HOME/fs_explorer}"

# Path to the project on the host (MSYS2 / Git-Bash style).
HOST_PROJECT="${HOST_PROJECT:-/d/c++/LTHT/file-system-explorer}"
