# frozen_string_literal: true

# -*- mode: ruby -*-
# vi: set ft=ruby :

# SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
# Copyright (c) 2019, Jianshen Liu <jliu120@ucsc.edu>

require 'fileutils'

# Vagrant-libvirt supports Vagrant 1.5, 1.6, 1.7 and 1.8
#   https://github.com/vagrant-libvirt/vagrant-libvirt#installation
# The feature of trigger requires Vagrant version >= 2.1.0
#   https://www.vagrantup.com/docs/triggers/
Vagrant.require_version '>= 2.1.0'

# All Vagrant configuration is done below. The "2" in Vagrant.configure
# configures the configuration version (we support older styles for
# backwards compatibility). Please don't change it unless you know what
# you're doing.
Vagrant.configure('2') do |config|
  # The most common configuration options are documented and commented below.
  # For a complete reference, please see the online documentation at
  # https://docs.vagrantup.com.

  # Every Vagrant development environment requires a box. You can search for
  # boxes at https://vagrantcloud.com/search.
  config.vm.box = 'generic/ubuntu1804'

  config.trigger.before :up do |trigger|
    trigger.info = '[WARNING] This setup process needs ~10 GB additional disk space.'
  end

  # Disable automatic box update checking. If you disable this, then
  # boxes will only be checked for updates when the user runs
  # `vagrant box outdated`. This is not recommended.
  # config.vm.box_check_update = false

  # Create a forwarded port mapping which allows access to a specific port
  # within the machine from a port on the host machine. In the example below,
  # accessing "localhost:8080" will access port 80 on the guest machine.
  # NOTE: This will enable public access to the opened port
  # config.vm.network "forwarded_port", guest: 80, host: 8080

  # Create a forwarded port mapping which allows access to a specific port
  # within the machine from a port on the host machine and only allow access
  # via 127.0.0.1 to disable public access
  # config.vm.network "forwarded_port", guest: 80, host: 8080, host_ip: "127.0.0.1"

  # Create a private network, which allows host-only access to the machine
  # using a specific IP.
  # config.vm.network "private_network", ip: "192.168.33.10"

  # Create a public network, which generally matched to bridged network.
  # Bridged networks make the machine appear as another physical device on
  # your network.
  # config.vm.network "public_network"

  # Share an additional folder to the guest VM. The first argument is
  # the path on the host to the actual folder. The second argument is
  # the path on the guest to mount the folder. And the optional third
  # argument is a set of non-required options.
  # config.vm.synced_folder "../data", "/vagrant_data"
  SyncedFolder = Struct.new(:hostpath, :guestpath)

  synced_folders = [SyncedFolder.new(Dir.pwd, '/vagrant')]
  synced_folders.each do |folder|
    config.vm.synced_folder folder.hostpath.to_s, folder.guestpath.to_s, type: 'nfs', mount_options: ['rw', 'vers=3', 'tcp', 'actimeo=2']
  end

  # Host debuggee folder for hosting the kernel source and the debug symbol
  # archive
  debuggee_folder_name = 'debuggee'
  FileUtils.mkdir_p Dir.pwd + '/' + debuggee_folder_name

  # Provider-specific configuration so you can fine-tune various
  # backing providers for Vagrant. These expose provider-specific options.
  # Example for VirtualBox:
  #
  # config.vm.provider "virtualbox" do |vb|
  #   # Display the VirtualBox GUI when booting the machine
  #   vb.gui = true
  #
  #   # Customize the amount of memory on the VM:
  #   vb.memory = "1024"
  # end
  #
  # View the documentation for the provider you are using for more
  # information on available options.

  config.vm.provider :libvirt do |libvirt|
    libvirt.memory = '4096'
    libvirt.cpus = '6'

    # Enable the gdb stub of QEMU/KVM.
    # Change the debugging port if the default doesn't work.
    libvirt.qemuargs value: '-gdb'
    libvirt.qemuargs value: 'tcp::1234'
  end

  # Enable X11 forwarding over SSH connections
  config.ssh.forward_x11 = true

  auto_mount_command = "echo >> /etc/fstab\n"
  synced_folders.each do |folder|
    auto_mount_command += <<-MOUNT_COMMAND
    mount -t nfs | grep -w "#{folder.guestpath}" | awk \
        '{ print $1 "\\t" $3 "\\t" $5 "\\t" substr($6, 2, length($6) - 2) 0 "\\t" 0 }' >> /etc/fstab
    MOUNT_COMMAND
  end

  # Enable provisioning with a shell script. Additional provisioners such as
  # Puppet, Chef, Ansible, Salt, and Docker are also available. Please see the
  # documentation for more information about their specific syntax and use.
  config.vm.provision 'shell', inline: <<~SHELL
    set -eu -o pipefail

    echo "[INFO] Add mountpoints to /etc/fstab"
    #{auto_mount_command}

    echo "[INFO] Installing kernel debug symbol package"
    echo "deb http://ddebs.ubuntu.com $(lsb_release -cs) main restricted universe multiverse
    deb http://ddebs.ubuntu.com $(lsb_release -cs)-updates main restricted universe multiverse" >> \
    /etc/apt/sources.list.d/ddebs.list
    apt install ubuntu-dbgsym-keyring
    apt-get update
    apt-get -y install linux-image-"$(uname -r)"-dbgsym
    cp -r /usr/lib/debug/boot/vmlinux-"$(uname -r)" /vagrant/#{debuggee_folder_name}/

    kernel_version="$(uname -v | grep -oP '#\\K\\d+')"
    package_version="$(uname -r | sed "s/-generic/.$kernel_version/")"
    kernel_release="$(uname -r | sed 's/-.*$//')"
    echo "[INFO] Installing linux-source-$kernel_release=$package_version"
    apt-get -y --no-install-recommends install linux-source-"$kernel_release"="$package_version"
    echo "[INFO] Extracting files from /usr/src/linux-source-$kernel_release.tar.bz2"
    tar -xf /usr/src/linux-source-"$kernel_release".tar.bz2 -C /vagrant/#{debuggee_folder_name}/ --no-same-owner

    echo "[INFO] Installing module build dependencies"
    apt-get -y --no-install-recommends install build-essential libelf-dev

    echo "[INFO] Clean up APT cache"
    apt-get clean && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

    echo "[INFO] Disable the kernel address space layout randomization (KASLR)"
    sed -i 's/\\(GRUB_CMDLINE_LINUX_DEFAULT=.*\\)"/\\1 nokaslr"/' /etc/default/grub
    update-grub

    echo "[INFO] Enable serial console service"
    systemctl enable serial-getty@ttyS0.service

    echo "[INFO] Update sshd_config to enable X11 forwarding"
    sed -i -r 's/.*(X11UseLocalhost ).*/\1no/' /etc/ssh/sshd_config

    echo "[INFO] Reboot to update system configuration"
    reboot

    echo "[INFO] Done system provision."
  SHELL
end
