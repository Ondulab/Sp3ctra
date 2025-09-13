#!/usr/bin/env bash
# setup_ethernet.sh - Configure Ethernet interface on Raspberry Pi (Debian/Bookworm)
# - Configures static Ethernet with specified IP
# - Manages routing priority for Ethernet

set -euo pipefail

ETHERNET_IP="192.168.100.10"
ETHERNET_METRIC="100"
ETHERNET_CONN="eth0-static"
ETHERNET_IFACE="eth0"

log() {
    echo "[setup_ethernet] $*"
}

fail() {
    echo "[setup_ethernet][ERROR] $*" >&2
    exit 1
}

need_root() {
    if [[ "${EUID}" -ne 0 ]]; then
        fail "Run as root (use: sudo $0 ...)"
    fi
}

parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --ip)
                ETHERNET_IP="${2:-}"; shift 2 ;;
            --interface)
                ETHERNET_IFACE="${2:-}"; shift 2 ;;
            --metric)
                ETHERNET_METRIC="${2:-}"; shift 2 ;;
            --connection-name)
                ETHERNET_CONN="${2:-}"; shift 2 ;;
            -h|--help)
                echo "Usage: sudo $0 [--ip IP] [--interface IFACE] [--metric METRIC] [--connection-name NAME]"
                echo ""
                echo "Options:"
                echo "  --ip IP               Static IP address (default: 192.168.100.10)"
                echo "  --interface IFACE     Ethernet interface name (default: eth0)"
                echo "  --metric METRIC       Routing metric (default: 100)"
                echo "  --connection-name NAME Connection name (default: eth0-static)"
                exit 0 ;;
            *)
                fail "Unknown argument: $1" ;;
        esac
    done

    # Validate IP format (basic check)
    if [[ ! "${ETHERNET_IP}" =~ ^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$ ]]; then
        fail "Invalid IP format: ${ETHERNET_IP}"
    fi
}

ensure_tools() {
    command -v nmcli >/dev/null 2>&1 || fail "nmcli not found (install NetworkManager)"
    
    systemctl is-enabled NetworkManager >/dev/null 2>&1 || {
        log "Enabling NetworkManager service at boot"
        systemctl enable NetworkManager
    }
    
    systemctl is-active --quiet NetworkManager || {
        log "Starting NetworkManager"
        systemctl start NetworkManager
    }
}

configure_networkmanager_security() {
    log "Configuring NetworkManager security settings for Ethernet"
    
    # Create NetworkManager configuration if it doesn't exist
    mkdir -p /etc/NetworkManager/conf.d/
    
    if [[ ! -f "/etc/NetworkManager/conf.d/99-disable-autoconnect.conf" ]]; then
        cat > /etc/NetworkManager/conf.d/99-disable-autoconnect.conf << 'EOF'
[main]
# Disable automatic connection creation
no-auto-default=*

[connection]
# Disable automatic connection for new devices
autoconnect-priority=-1

[device]
# Only manage explicitly configured devices for WiFi
wifi.scan-rand-mac-address=no

[logging]
# Enable connection logging for security auditing
level=INFO
domains=WIFI,DEVICE
EOF
        log "NetworkManager security configuration applied"
    else
        log "NetworkManager security configuration already exists"
    fi
}

configure_ethernet() {
    log "Configuring Ethernet interface (${ETHERNET_IFACE}) with IP ${ETHERNET_IP}"
    
    # Remove any existing connection with the same name
    nmcli connection delete "${ETHERNET_CONN}" 2>/dev/null || true
    
    # Create new Ethernet connection with static IP
    nmcli connection add \
        type ethernet \
        con-name "${ETHERNET_CONN}" \
        ifname "${ETHERNET_IFACE}" \
        ipv4.method manual \
        ipv4.addresses "${ETHERNET_IP}/24" \
        ipv4.route-metric "${ETHERNET_METRIC}" \
        connection.autoconnect yes
    
    log "Ethernet configured with IP ${ETHERNET_IP}, metric ${ETHERNET_METRIC}"
}

activate_ethernet() {
    log "Activating Ethernet connection"
    
    if nmcli connection up "${ETHERNET_CONN}" ifname "${ETHERNET_IFACE}"; then
        log "Ethernet connection activated successfully"
        
        # Wait for interface to be ready
        sleep 2
        
        # Verify connectivity
        if ping -c 1 -W 3 8.8.8.8 >/dev/null 2>&1; then
            log "Ethernet connectivity verified (ping successful)"
        else
            log "Warning: Ethernet connected but no internet connectivity detected"
        fi
    else
        fail "Failed to activate Ethernet connection"
    fi
}

show_ethernet_status() {
    echo
    log "Ethernet device status:"
    nmcli device status | grep -E "(DEVICE|${ETHERNET_IFACE})" || true
    
    echo
    log "Ethernet IP configuration:"
    ip addr show "${ETHERNET_IFACE}" 2>/dev/null || true
    
    echo
    log "Routing table (Ethernet routes):"
    ip route | grep "${ETHERNET_IFACE}" || true
}

main() {
    parse_args "$@"
    need_root
    export SYSTEMD_PAGER=cat
    export SYSTEMD_LESS=
    
    ensure_tools
    configure_networkmanager_security
    configure_ethernet
    activate_ethernet
    show_ethernet_status
    
    log "Ethernet configuration completed. Changes will persist after reboot."
}

main "$@"
