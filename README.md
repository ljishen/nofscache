# nofscache

Eliminating caching effects for I/Os to a storage device

This repository contains a Vagrantfile used to start an Ubuntu 18.04 environment for testing.


## How to start the vagrant environment

1. Download and install Vagrant
   ```bash
   curl -s https://releases.hashicorp.com/vagrant/2.2.5/vagrant_2.2.5_x86_64.deb -o /tmp/vagrant_x86_64.deb
   sudo dpkg -i /tmp/vagrant_x86_64.deb
   ```

2. This vagrant environment uses the libvirt provider. We need to have all the build dependencies installed in order to use [vagrant-libvirt](https://github.com/vagrant-libvirt/vagrant-libvirt)
   ```bash
   sudo apt-get install qemu libvirt-bin ebtables dnsmasq-base
   sudo apt-get install libxslt-dev libxml2-dev libvirt-dev zlib1g-dev ruby-dev
   ```

3. You're ready to install vagrant-libvirt using standard [Vagrant plugin](http://docs.vagrantup.com/v2/plugins/usage.html) installation methods
   ```bash
   vagrant plugin install vagrant-libvirt
   ```

4. Install NFS server because we need NFS to set up synced folder
   ```bash
   sudo apt-get install nfs-kernel-server
   ```

5. Start the vagrant environment
   ```bash
   vagrant up
   ```

6. SSH connect to the machine
   ```bash
   vagrant ssh
   ```
