# OpenMAG
Open Mobile Aggregation Gateway, which is a method to setup the multipath aggregation gateway based on general Linux Kernel.

## Prerequisites

Before proceeding with the MPTCP configuration, ensure that:

- **Operating System:** >= Ubuntu 22.04
- **Kernel Version:** 6.8.0-52-generic

We need four machines to set up the testbed:

- Client : at least have a single WAN interface
- Local GW (Gateway) : Local GW **must** have at least two WAN interfaces to set up multipath.
- Remote GW (Gateway) : at least have a single WAN interface
- Server : at better the `apache2` service installed and running, with files available for download under the `/var/www/html/` directory.

**Topology Example:**

Below is a visual representation of the network topology:

![Network Topology](topology.jpg)

## Installation

The Local & Remote GW must install the kernel version with MPTCP v1.0, and OpenVPN >= v2.6.8

### MPTCP v1.0 Kernel Installation

Kernel installation:

1.  Install the generic HWE kernel:

    ```bash
    apt install linux-generic-hwe-22.04
    ```

2.  Edit the GRUB configuration file:

    ```bash
    vim /etc/default/grub
    ```

    Modify the following lines:

    ```
    GRUB_DEFAULT="Advanced options for Ubuntu>Ubuntu, with Linux 6.8.0-52-generic"
    GRUB_TIMEOUT_STYLE=menu
    GRUB_TIMEOUT=5
    GRUB_CMDLINE_LINUX_DEFAULT="quiet splash ipv6.disable=1"
    GRUB_CMDLINE_LINUX="ipv6.disable=1"
    ```

3.  Update GRUB and reboot:

    ```bash
    update-grub
    reboot
    ```

4.  Verify the kernel version:

    ```bash
    uname -r
    ```

    Expected output:

    ```
    6.8.0-52-generic
    ```

### OpenVPN Installation

As kernel 6.8.x has applied openssl 3.x, the openvpn version must be above 2.6.8 to adapt to new openssl. We choose 2.6.10 for example.

1.  Install dependencies:
    ```bash
    apt install easy-rsa libssl-dev liblz4-dev liblzo2-dev libpam0g-dev cmake autoconf libtool libnl-3-dev libnl-genl-3-dev libcap-ng-dev pkg-config
    ```
2.  Download OpenVPN 2.6.10:

    ```bash
    wget https://github.com/OpenVPN/openvpn/releases/download/v2.6.10/openvpn-2.6.10.tar.gz

    tar -xvf openvpn-2.6.10.tar.gz

    cd openvpn-2.6.10
    ```

3.  Compile and install openvpn:

    ```bash
    autoreconf -i -v -f

    ./configure

    make && make install
    ```

## Configuration

### Remote GW Configuration

IP Address 1: 10.1.1.100

Gateway: 10.1.1.254

**Key and Crt files:**

1.  Generate certificates:

    ```bash
    cd /etc/openvpn
    ln -s /usr/share/easy-rsa easy-rsa
    cd easy-rsa
    ./easyrsa init-pki
    ./easyrsa build-ca nopass
    ./easyrsa gen-req server nopass
    ./easyrsa gen-req client nopass
    ./easyrsa sign-req server server
    ./easyrsa sign-req client client
    cp pki/ca.crt /etc/openvpn/
    cp pki/issued/server.crt /etc/openvpn/
    cp pki/private/server.key /etc/openvpn/
    ```

2.  Copy certificates to Local GW:

    Copy `ca.crt`, `client.key`, `client.crt` to the local gw: `/etc/openvpn/`

**Openvpn Server configuration:**

1.  Copy `dh2048.pem`:

    ```bash
    cp /usr/share/doc/openvpn/examples/sample-keys/dh2048.pem /etc/openvpn/
    ```

2.  Edit `/etc/openvpn/server.conf`:

    ```bash
    vim /etc/openvpn/server.conf
    ```

    ```
    local 10.1.1.100
    port 52115
    dev tun
    ca ca.crt
    cert server.crt
    key server.key
    dh dh2048.pem
    server 10.8.0.0 255.255.255.0
    ifconfig-pool-persist /var/log/openvpn/ipp.txt
    push "route 10.1.1.0 255.255.255.0"
    keepalive 10 120
    user nobody
    group nogroup
    persist-key
    persist-tun
    status /var/log/openvpn/openvpn-status.log
    verb 3
    tun-mtu 1400
    mssfix 1360
    ```

**Sysctl and iptables:**

```bash
sysctl -w net.ipv4.ip_forward=1
##enp1s0f0 is the interface with IP 10.1.1.100
iptables -t nat -A POSTROUTING -s 10.8.0.0/24 -o enp1s0f0 -j MASQUERADE
```

### Local Gw Configuration

IP Address 1: 10.1.2.100

Gateway: 10.1.2.254

IP Address 2: 10.1.3.100

Gateway: 10.1.3.254

**Openvpn Client configuration:**

Edit `/etc/openvpn/client.conf`:

    ```bash
    vim /etc/openvpn/client.conf
    ```

    ```
    client
    dev tun
    proto tcp
    remote 10.1.1.100 52115
    ca ca.crt
    cert client.crt
    key client.key
    resolv-retry infinite
    nobind
    user nobody
    group nogroup
    persist-key
    persist-tun
    status /var/log/openvpn/openvpn-status.log
    verb 3
    ```

**Sysctl and iptables:**

```bash
sysctl -w net.ipv4.ip_forward=1
## enp0s31f6 is the interface connected with public network 192.168.10.x
iptables -t nat -A POSTROUTING -o enp0s31f6 -s 10.8.0.0/24 -j MASQUERADE
iptables -t nat -A POSTROUTING -o tun0 -j MASQUERADE
ip route add 30.1.1.100/32 via 10.1.2.254 dev enp3s0f0 metric 1
ip route add default via 10.1.2.254 dev enp3s0f0 metric 9
ip route add default via 10.1.3.254 dev enp3s0f1 metric 10
ip route replace 10.1.1.100/32 metric 1 nexthop via 10.1.2.254 dev enp1s0f0 weight 1  nexthop via 10.1.3.254 dev enp1s0f1 weight 1
```

**Setup multipath**

```bash
ip mptcp limits set subflow 8 add_addr_accepted 8

ip rule add from 10.1.2.100 table 1
ip rule add from 10.1.3.100 table 2
ip mptcp endpoint flush
ip mptcp endpoint add 10.1.2.100 dev enp1s0f0 subflow
ip mptcp endpoint add 10.1.3.100 dev enp1s0f1 subflow
ip mptcp endpoint show
```

## Run Openvpn with multipath

Examples of how to use the project.

### Remote GW

```bash
cd /etc/openvpn
mptcpize run openvpn --config server.conf
iperf3 -s
```

### Local GW

```bash
cd /etc/openvpn
mptcpize run openvpn --config client.conf
iperf3 -c 10.1.1.200 -R
```

Use nload tool to show the multipath traffic from two interfaces
```bash
nload -m
```

