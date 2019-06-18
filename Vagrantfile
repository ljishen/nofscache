# -*- mode: ruby -*-
# vi: set ft=ruby :

# SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
# Copyright (c) 2019, Jianshen Liu <jliu120@ucsc.edu>

require 'digest/md5'
require 'etc'
require 'fileutils'

# Vagrant-libvirt supports Vagrant 1.5, 1.6, 1.7 and 1.8
#   https://github.com/vagrant-libvirt/vagrant-libvirt#installation
# The feature of trigger requires Vagrant version >= 2.1.0
#   https://www.vagrantup.com/docs/triggers/
Vagrant.require_version ">= 2.1.0"

# All Vagrant configuration is done below. The "2" in Vagrant.configure
# configures the configuration version (we support older styles for
# backwards compatibility). Please don't change it unless you know what
# you're doing.
Vagrant.configure("2") do |config|
  # The most common configuration options are documented and commented below.
  # For a complete reference, please see the online documentation at
  # https://docs.vagrantup.com.

  # Every Vagrant development environment requires a box. You can search for
  # boxes at https://vagrantcloud.com/search.
  config.vm.box = "generic/ubuntu1804"

  config.trigger.before :up do |trigger|
    trigger.info = "WARNING: this setup process needs ~10 GB additional disk space."
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
  SyncedFolder = Struct.new(:hostpath, :guestpath) do
    def mount_tag
      # https://github.com/vagrant-libvirt/vagrant-libvirt/blob/master/lib/vagrant-libvirt/cap/mount_p9.rb
      Digest::MD5.new.update(hostpath).to_s[0, 31]
    end
  end

  pwuid = Etc.getpwuid()

  synced_folders = [SyncedFolder.new(Dir.pwd, "/vagrant")]
  synced_folders.each do |folder|
    config.vm.synced_folder "#{folder.hostpath}", "#{folder.guestpath}", type: "9p", accessmode: "mapped", mount: true
  end

  # Host debuggee folder for hosting the kernel source and the debug symbol
  # archive
  debuggee_folder_name = "debuggee"
  FileUtils.mkdir_p Dir.pwd + "/" + debuggee_folder_name

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
    libvirt.memory = "4096"
    libvirt.cpus = "6"

    # Enable the gdb stub of QEMU/KVM.
    # Change the debugging port if the default doesn't work.
    libvirt.qemuargs :value => "-gdb"
    libvirt.qemuargs :value => "tcp::1234"
  end

  commands = ""
  synced_folders.each do |folder|
    commands += <<-SH_SCRIPT
mkdir -p #{folder.guestpath}
if ! findmnt "#{folder.guestpath}" > /dev/null 2>&1; then
    mount -t 9p -o trans=virtio,version=9p2000.L "#{folder.mount_tag}" "#{folder.guestpath}"
fi
SH_SCRIPT
  end

  # Enable provisioning with a shell script. Additional provisioners such as
  # Puppet, Chef, Ansible, Salt, and Docker are also available. Please see the
  # documentation for more information about their specific syntax and use.
  config.vm.provision "shell", inline: <<~SHELL
    # Add a login user to match the permission of the current user on the host
    adduser -disabled-password --gecos "" --uid #{pwuid.uid} #{pwuid.name}
    echo "#{pwuid.name}:password" | chpasswd
    usermod -aG sudo #{pwuid.name}
    cp -r /home/vagrant/.ssh /home/#{pwuid.name}/
    chown -R #{pwuid.name}:#{pwuid.name} /home/#{pwuid.name}/.ssh
    sed 's/^[[:alnum:]]*/#{pwuid.name}/' /etc/sudoers.d/vagrant > /etc/sudoers.d/#{pwuid.name}
    chmod 440 /etc/sudoers.d/#{pwuid.name}

    # Install kernel debug symbol package
    echo "deb http://ddebs.ubuntu.com $(lsb_release -cs) main restricted universe multiverse
    deb http://ddebs.ubuntu.com $(lsb_release -cs)-updates main restricted universe multiverse" >> \
    /etc/apt/sources.list.d/ddebs.list
    apt install ubuntu-dbgsym-keyring
    apt-get update
    apt-get -y install linux-image-$(uname -r)-dbgsym
    cp -r /usr/lib/debug/boot/vmlinux-$(uname -r) /vagrant/#{debuggee_folder_name}/

    # Download Linux kernel source
    kernel_version="$(uname -v | grep -oP '#\\K\\d+')"
    package_version="$(uname -r | sed "s/-generic/.$kernel_version/")"
    kernel_release="$(uname -r | sed 's/-.*$//')"
    apt-get -y --no-install-recommends install linux-source-"$kernel_release"="$package_version"
    tar -xf /usr/src/linux-source-*.tar.bz2 -C /vagrant/#{debuggee_folder_name}/

    # Install module build dependencies
    apt-get -y --no-install-recommends install build-essential libelf-dev

    # Clean up
    apt-get clean && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

    # Disable the kernel address space layout randomization (KASLR) on the guest
    sed -i 's/\\(GRUB_CMDLINE_LINUX_DEFAULT=.*\\)"/\\1 nokaslr"/' /etc/default/grub
    update-grub

    # Add a cron job for mounting 9p shared path after system reboot
    cat <<'SCRIPT_EOF' > /usr/local/sbin/vagrant_mount
    #!/bin/sh
    # SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
    # Copyright (c) 2019, Jianshen Liu <jliu120@ucsc.edu>

    set -e

    modprobe 9p
    modprobe 9pnet_virtio

    #{commands}
    # Remove cron job once mount finished
    rm -f /etc/cron.d/vagrant_mount
    SCRIPT_EOF

    chmod +x /usr/local/sbin/vagrant_mount

    cat <<'CRON_EOF' > /etc/cron.d/vagrant_mount
    # SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
    # Copyright (c) 2019, Jianshen Liu <jliu120@ucsc.edu>

    SHELL=/bin/sh
    PATH=/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin

    @reboot root vagrant_mount
    CRON_EOF

    # Configure a serial console in the guest
    systemctl enable serial-getty@ttyS0.service

    # Reboot to update system configuration
    reboot
  SHELL

  if ARGV[0] == "ssh"
    config.ssh.username = pwuid.name
  end
end
