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
   sudo apt-get -y update
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


## Troubleshooting

- If you see something like the following error during `vagrant up`:
  ```bash
  ==> default: Mounting NFS shared folders...
  The following SSH command responded with a non-zero exit status.
  Vagrant assumes that this means the command failed!

  mount -o vers=3,udp,rw,vers=3,tcp,actimeo=2 192.168.121.1:/users/ljishen/nofscache /vagrant

  Stdout from the command:



  Stderr from the command:

  mount.nfs: access denied by server while mounting 192.168.121.1:/users/ljishen/nofscache
  ```

  Try to add this rule to file `/etc/hosts.allow`
  ```bash
  $ echo "rpcbind: 192.168.121." >> /etc/hosts.allow
  ```
  As the default `management_network_address` used by `vagrant-libvirt` is `192.168.121.0/24`. See [management_network_address](https://github.com/vagrant-libvirt/vagrant-libvirt#management-network) for more details.
