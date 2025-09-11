#!/usr/bin/env bash
# setup_network.sh - Configure network interfaces on Raspberry Pi (Debian/Bookworm)
# - Configures static Ethernet (192.168.100.10)
# - Sets up WiFi with provided credentials
# - Manages routing priorities (Ethernet preferred)

set -euo pipefail

SSID=""
PSK=""
COUNTRY="FR"
ETHERNET_IP="192.168.100.10"
ETHERNET_METRIC="100"
WIFI_METRIC="200"
ETHERNET_CONN="eth0-static"
WIFI_CONN="wifi-managed"
WIFI_IFACE="wlan0"
ETHERNET_IFACE="eth0"

log() {
    echo "[setup_network] $*"
}

fail() {
    echo "[setup_network][ERROR] $*" >&2
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
            --ssid)
                SSID="${2:-}"; shift 2 ;;
            --psk)
                PSK="${2:-}"; shift 2 ;;
            --country)
                COUNTRY="${2:-}"; shift 2 ;;
            -h|--help)
                echo "Usage: sudo $0 --ssid \"WIFI_NAME\" --psk \"WIFI_PASSWORD\" [--country FR]"
                exit 0 ;;
            *)
                fail "Unknown argument: $1" ;;
        esac
    done

    [[ -n "${SSID}" ]] || fail "--ssid is required"
    [[ -n "${PSK}" ]] || fail "--psk is required"
}

ensure_tools() {
    command -v nmcli >/dev/null 2>&1 || fail "nmcli not found (install NetworkManager)"
    command -v iw >/dev/null 2>&1 || fail "iw not found (apt install iw)"
    
    systemctl is-enabled NetworkManager >/dev/null 2>&1 || {
        log "Enabling NetworkManager service at boot"
        systemctl enable NetworkManager
    }
    
    systemctl is-active --quiet NetworkManager || {
        log "Starting NetworkManager"
        systemctl start NetworkManager
    }
}

set_country() {
    log "Setting regulatory domain to ${COUNTRY}"
    iw reg set "${COUNTRY}" || log "iw reg set ${COUNTRY} failed (non-fatal)"
}

configure_ethernet() {
    log "Configuring Ethernet interface (${ETHERNET_IFACE})"
    
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
    
    log "Ethernet configured with IP ${ETHERNET_IP}"
}

configure_wifi() {
    log "Configuring WiFi interface (${WIFI_IFACE})"
    
    # Enable WiFi radio
    nmcli radio wifi on
    
    # Ensure interface is managed
    nmcli device set "${WIFI_IFACE}" managed yes
    
    # Remove any existing connection with the same name
    nmcli connection delete "${WIFI_CONN}" 2>/dev/null || true
    
    # Create new WiFi connection
    nmcli connection add \
        type wifi \
        con-name "${WIFI_CONN}" \
        ifname "${WIFI_IFACE}" \
        ssid "${SSID}" \
        wifi-sec.key-mgmt wpa-psk \
        wifi-sec.psk "${PSK}" \
        ipv4.method auto \
        ipv4.route-metric "${WIFI_METRIC}" \
        connection.autoconnect yes
    
    log "WiFi configured for SSID: ${SSID}"
}

activate_connections() {
    log "Activating network connections"
    
    # Activate Ethernet
    if ! nmcli connection up "${ETHERNET_CONN}" ifname "${ETHERNET_IFACE}"; then
        log "Warning: Failed to activate Ethernet connection (non-fatal)"
    fi
    
    # Scan and activate WiFi
    nmcli device wifi rescan || true
    if ! nmcli connection up "${WIFI_CONN}" ifname "${WIFI_IFACE}"; then
        log "Warning: Failed to activate WiFi connection. Check SSID/password."
    fi
}

show_status() {
    echo
    log "Network device status:"
    nmcli device status || true
    
    echo
    log "IP configuration:"
    ip addr show || true
    
    echo
    log "Routing table:"
    ip route || true
}

main() {
    need_root
    parse_args "$@"
    export SYSTEMD_PAGER=cat
    export SYSTEMD_LESS=
    
    ensure_tools
    set_country
    configure_ethernet
    configure_wifi
    activate_connections
    show_status
    
    log "Network configuration completed. Changes will persist after reboot."
}

main "$@"
