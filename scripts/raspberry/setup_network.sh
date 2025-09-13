#!/usr/bin/env bash
# setup_network.sh - Network configuration orchestrator for Raspberry Pi
# - Orchestrates Ethernet and WiFi configuration using specialized scripts
# - Maintains backward compatibility with original interface
# - Manages routing priorities (Ethernet preferred over WiFi)

set -euo pipefail

# Default parameters
SSID=""
PSK=""
COUNTRY="FR"
WIFI_BAND="auto"
ETHERNET_IP="192.168.100.10"
ETHERNET_METRIC="100"
WIFI_METRIC="200"
ETHERNET_IFACE="eth0"
WIFI_IFACE="wlan0"

# Execution modes
CONFIGURE_ETHERNET=true
CONFIGURE_WIFI=true

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

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
            --band)
                WIFI_BAND="${2:-}"; shift 2 ;;
            --ethernet-ip)
                ETHERNET_IP="${2:-}"; shift 2 ;;
            --ethernet-interface)
                ETHERNET_IFACE="${2:-}"; shift 2 ;;
            --wifi-interface)
                WIFI_IFACE="${2:-}"; shift 2 ;;
            --ethernet-metric)
                ETHERNET_METRIC="${2:-}"; shift 2 ;;
            --wifi-metric)
                WIFI_METRIC="${2:-}"; shift 2 ;;
            --ethernet-only)
                CONFIGURE_ETHERNET=true
                CONFIGURE_WIFI=false
                shift ;;
            --wifi-only)
                CONFIGURE_ETHERNET=false
                CONFIGURE_WIFI=true
                shift ;;
            -h|--help)
                show_help
                exit 0 ;;
            *)
                fail "Unknown argument: $1" ;;
        esac
    done

    # Validate requirements based on configuration mode
    if [[ "${CONFIGURE_WIFI}" == "true" ]]; then
        [[ -n "${SSID}" ]] || fail "--ssid is required for WiFi configuration"
        [[ -n "${PSK}" ]] || fail "--psk is required for WiFi configuration"
        
        # Validate band parameter
        case "${WIFI_BAND}" in
            auto|2g|5g) ;;
            *) fail "Invalid --band value. Use: auto, 2g, or 5g" ;;
        esac
    fi
}

show_help() {
    cat << 'EOF'
Usage: sudo setup_network.sh [OPTIONS]

Network configuration orchestrator that uses modular scripts for Ethernet and WiFi setup.

CONFIGURATION MODES:
  (default)             Configure both Ethernet and WiFi
  --ethernet-only       Configure Ethernet interface only
  --wifi-only           Configure WiFi interface only

WIFI OPTIONS (required when configuring WiFi):
  --ssid SSID          WiFi network name
  --psk PASSWORD       WiFi password

OPTIONAL PARAMETERS:
  --country CODE       Country code for WiFi regulations (default: FR)
  --band BAND          WiFi band: auto|2g|5g (default: auto)
                       auto: 2g for PRE_WIFI, 5g for PRE_WIFI_5GHZ, default 2g for others

NETWORK INTERFACE OPTIONS:
  --ethernet-ip IP     Static IP for Ethernet (default: 192.168.100.10)
  --ethernet-interface Ethernet interface name (default: eth0)
  --wifi-interface     WiFi interface name (default: wlan0)

ROUTING PRIORITY OPTIONS:
  --ethernet-metric N  Ethernet routing metric (default: 100, lower = higher priority)
  --wifi-metric N      WiFi routing metric (default: 200, lower = higher priority)

EXAMPLES:
  # Configure both Ethernet and WiFi (default behavior)
  sudo ./setup_network.sh --ssid "MyWiFi" --psk "password123"

  # Configure Ethernet only with custom IP
  sudo ./setup_network.sh --ethernet-only --ethernet-ip 192.168.1.100

  # Configure WiFi only with specific band
  sudo ./setup_network.sh --wifi-only --ssid "MyWiFi_5G" --psk "password" --band 5g

  # Full configuration with custom parameters
  sudo ./setup_network.sh --ssid "Office_WiFi" --psk "secret" \
       --ethernet-ip 10.0.1.50 --country US --band auto

NOTES:
  - Ethernet is prioritized over WiFi by default (lower metric = higher priority)
  - This script orchestrates setup_ethernet.sh and setup_wifi.sh
  - Changes persist after reboot
  - Requires NetworkManager to be installed
EOF
}

check_subscripts() {
    local missing=false
    
    if [[ "${CONFIGURE_ETHERNET}" == "true" ]] && [[ ! -f "${SCRIPT_DIR}/setup_ethernet.sh" ]]; then
        log "ERROR: setup_ethernet.sh not found in ${SCRIPT_DIR}/"
        missing=true
    fi
    
    if [[ "${CONFIGURE_WIFI}" == "true" ]] && [[ ! -f "${SCRIPT_DIR}/setup_wifi.sh" ]]; then
        log "ERROR: setup_wifi.sh not found in ${SCRIPT_DIR}/"
        missing=true
    fi
    
    if [[ "${missing}" == "true" ]]; then
        fail "Required script files are missing. Ensure all network setup scripts are present."
    fi
}

configure_ethernet() {
    log "=== CONFIGURING ETHERNET ==="
    
    local ethernet_args=(
        --ip "${ETHERNET_IP}"
        --interface "${ETHERNET_IFACE}"
        --metric "${ETHERNET_METRIC}"
    )
    
    log "Running: ${SCRIPT_DIR}/setup_ethernet.sh ${ethernet_args[*]}"
    
    if "${SCRIPT_DIR}/setup_ethernet.sh" "${ethernet_args[@]}"; then
        log "Ethernet configuration completed successfully"
    else
        fail "Ethernet configuration failed"
    fi
}

configure_wifi() {
    log "=== CONFIGURING WIFI ==="
    
    local wifi_args=(
        --ssid "${SSID}"
        --psk "${PSK}"
        --country "${COUNTRY}"
        --band "${WIFI_BAND}"
        --interface "${WIFI_IFACE}"
        --metric "${WIFI_METRIC}"
    )
    
    log "Running: ${SCRIPT_DIR}/setup_wifi.sh ${wifi_args[*]}"
    
    if "${SCRIPT_DIR}/setup_wifi.sh" "${wifi_args[@]}"; then
        log "WiFi configuration completed successfully"
    else
        fail "WiFi configuration failed"
    fi
}

show_final_status() {
    echo
    log "=== FINAL NETWORK STATUS ==="
    
    echo
    log "Network device status:"
    nmcli device status || true
    
    echo  
    log "Active connections:"
    nmcli connection show --active || true
    
    echo
    log "IP configuration:"
    ip addr show || true
    
    echo
    log "Routing table (priority: lower metric = higher priority):"
    ip route || true
    
    echo
    log "Testing connectivity..."
    if ping -c 1 -W 3 8.8.8.8 >/dev/null 2>&1; then
        log "Internet connectivity: OK"
    else
        log "Internet connectivity: FAILED or LIMITED"
    fi
}

main() {
    parse_args "$@"
    need_root
    export SYSTEMD_PAGER=cat
    export SYSTEMD_LESS=
    
    log "Network configuration orchestrator starting..."
    log "Mode: Ethernet=${CONFIGURE_ETHERNET}, WiFi=${CONFIGURE_WIFI}"
    
    check_subscripts
    
    # Configure interfaces in order (Ethernet first for priority)
    if [[ "${CONFIGURE_ETHERNET}" == "true" ]]; then
        configure_ethernet
        echo
    fi
    
    if [[ "${CONFIGURE_WIFI}" == "true" ]]; then
        configure_wifi
        echo
    fi
    
    # Wait for all configurations to settle
    log "Waiting for network configuration to settle..."
    sleep 3
    
    show_final_status
    
    log "Network configuration orchestration completed successfully."
    log "All changes will persist after reboot."
}

main "$@"
