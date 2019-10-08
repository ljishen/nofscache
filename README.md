# nofscache

This repository contains a Vagrantfile used to start an Ubuntu 18.04.3 LTS environment for testing.


## How to start the Vagrant environment

1. Download and install Vagrant
   ```bash
   curl -s https://releases.hashicorp.com/vagrant/2.2.5/vagrant_2.2.5_x86_64.deb -o /tmp/vagrant_x86_64.deb
   sudo dpkg -i /tmp/vagrant_x86_64.deb
   ```

2. This Vagrant environment uses the libvirt provider. We need to have all the build dependencies installed in order to use [vagrant-libvirt](https://github.com/vagrant-libvirt/vagrant-libvirt)
   ```bash
   sudo apt-get update
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

5. Install NFS server since we use NFS to set up synced folders
   ```bash
   sudo apt-get -y install nfs-kernel-server
   ```

6. Start the Vagrant environment
   ```bash
   git clone https://github.com/ljishen/nofscache
   cd nofscache
   vagrant up
   ```

7. Connect to the virtual machine via SSH
   ```bash
   vagrant [-X] ssh
   ```
   or via serial console
   ```bash
   virsh console "$(virsh list --state-running --name | grep nofscache)"
   ```
    - Use `vagrant -X ssh` to connect with X11 forwarding enabled.
    - The default user is `vagrant` with password `vagrant`. To exit a virsh console session, type `Ctrl+]`.


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


## Miscellaneous

- **How to change the directory where Vagrant boxes are stored?**

  Set the [`VAGRANT_HOME`](https://www.vagrantup.com/docs/other/environmental-variables.html#vagrant_home) environmental variable.

- **How to change the default libvirt storage pool?**

    - Related Posts:
        - [Configure default KVM virtual storage on Redhat Linux](https://linuxconfig.org/configure-default-kvm-virtual-storage-on-redhat-linux)
        - [How to change the default Storage Pool from libvirt?](https://serverfault.com/questions/840519/how-to-change-the-default-storage-pool-from-libvirt)
        - [Storage Management](https://libvirt.org/storage.html)
    - The `permission denied error`:
        - [qemu-kvm: could not open disk image ' ': Permission denied](https://github.com/jedi4ever/veewee/issues/996)
        - [permission denied error for NFS image, should libvirt error message mention virt_use_nfs?](https://bugzilla.redhat.com/show_bug.cgi?id=589922)
