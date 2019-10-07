# nofscache

This repository contains a Vagrantfile used to start an Ubuntu 18.04 environment for testing.


## How to start the vagrant environment

1. Download and install Vagrant
   ```bash
   curl -s https://releases.hashicorp.com/vagrant/2.2.5/vagrant_2.2.5_x86_64.deb -o /tmp/vagrant_x86_64.deb
   sudo dpkg -i /tmp/vagrant_x86_64.deb
   ```

2. This vagrant environment uses the libvirt provider. We need to have all the build dependencies installed in order to use [vagrant-libvirt](https://github.com/vagrant-libvirt/vagrant-libvirt)
   ```bash
   sudo apt-get -y install qemu libvirt-bin ebtables dnsmasq-base
   sudo apt-get -y install libxslt-dev libxml2-dev libvirt-dev zlib1g-dev ruby-dev
   ```

3. Add your user to the "libvirt" group. Remember that you will have to log out and back in for this to take effect!
   ```bash
   sudo usermod -aG libvirt $USER
   ```

4. You're ready to install vagrant-libvirt using standard [Vagrant plugin](http://docs.vagrantup.com/v2/plugins/usage.html) installation methods
   ```bash
   vagrant plugin install vagrant-libvirt
   ```

5. Install NFS server because we need NFS to set up the synced folder
   ```bash
   sudo apt-get -y install nfs-kernel-server
   ```

6. Start the vagrant environment
   ```bash
   git clone https://github.com/ljishen/nofscache
   cd nofscache
   vagrant up
   ```

7. SSH connect to the machine
   ```bash
   vagrant ssh
   ```
