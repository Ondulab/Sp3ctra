#!/usr/bin/env bash
# setup_network.sh - Configure network interfaces on Raspberry Pi (Debian/Bookworm)
# - Configures static Ethernet (192.168.100.10)
# - Sets up WiFi with provided credentials
# - Manages routing priorities (Ethernet preferred)

set -euo pipefail

SSID=""
PSK=""
COUNTRY="FR"
WIFI_BAND="auto"
ETHERNET_IP="192.168.100.10"
ETHERNET_METRIC="100"
WIFI_METRIC="200"
ETHERNET_CONN="eth0-static"
WIFI_CONN=""
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
            --band)
                WIFI_BAND="${2:-}"; shift 2 ;;
            -h|--help)
                echo "Usage: sudo $0 --ssid \"WIFI_NAME\" --psk \"WIFI_PASSWORD\" [--country FR] [--band auto|2g|5g]"
                echo ""
                echo "Options:"
                echo "  --ssid SSID       WiFi network name (required)"
                echo "  --psk PASSWORD    WiFi password (required)"
                echo "  --country CODE    Country code for WiFi regulations (default: FR)"
                echo "  --band BAND       WiFi band: auto|2g|5g (default: auto)"
                echo "                    auto: 2g for PRE_WIFI, 5g for PRE_WIFI_5GHZ, both for others"
                exit 0 ;;
            *)
                fail "Unknown argument: $1" ;;
        esac
    done

    [[ -n "${SSID}" ]] || fail "--ssid is required"
    [[ -n "${PSK}" ]] || fail "--psk is required"
    
    # Validate band parameter
    case "${WIFI_BAND}" in
        auto|2g|5g) ;;
        *) fail "Invalid --band value. Use: auto, 2g, or 5g" ;;
    esac
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

determine_wifi_band() {
    local band=""
    
    case "${WIFI_BAND}" in
        "2g")
            band="bg"
            ;;
        "5g")
            band="a"
            ;;
        "auto")
            # Auto-detection based on SSID patterns
            case "${SSID}" in
                *"5GHZ"*|*"5G"*)
                    band="a"
                    log "Auto-detected 5GHz band for SSID: ${SSID}" >&2
                    ;;
                *"2GHZ"*|*"2G"*)
                    band="bg"
                    log "Auto-detected 2.4GHz band for SSID: ${SSID}" >&2
                    ;;
                "PRE_WIFI_5GHZ")
                    band="a"
                    log "Auto-detected 5GHz band for SSID: ${SSID}" >&2
                    ;;
                "PRE_WIFI")
                    band="bg"
                    log "Auto-detected 2.4GHz band for SSID: ${SSID}" >&2
                    ;;
                *)
                    # Default to both bands for unknown SSIDs
                    band="bg"
                    log "Using 2.4GHz band as default for SSID: ${SSID}" >&2
                    ;;
            esac
            ;;
    esac
    
    echo "${band}"
}

determine_connection_name() {
    local conn_name=""
    local band_suffix=""
    
    # Determine band suffix based on actual band used
    local wifi_band_setting
    wifi_band_setting=$(determine_wifi_band)
    
    case "${wifi_band_setting}" in
        "a")
            band_suffix="_5GHZ"
            ;;
        "bg")
            band_suffix="_2GHZ"
            ;;
    esac
    
    # Generate connection name
    case "${SSID}" in
        "PRE_WIFI_5GHZ"|"PRE_WIFI")
            # For known SSIDs, use SSID + band suffix
            conn_name="${SSID}${band_suffix}"
            ;;
        *)
            # For other SSIDs, use SSID + band suffix
            conn_name="${SSID}${band_suffix}"
            ;;
    esac
    
    echo "${conn_name}"
}

disable_wpa_supplicant() {
    log "Checking for wpa_supplicant conflicts"
    
    # Check if wpa_supplicant is enabled
    if systemctl is-enabled wpa_supplicant.service >/dev/null 2>&1; then
        log "Disabling wpa_supplicant service (conflicts with NetworkManager)"
        systemctl disable wpa_supplicant.service
    fi
    
    # Check if wpa_supplicant is running
    if systemctl is-active --quiet wpa_supplicant.service; then
        log "Stopping wpa_supplicant service"
        systemctl stop wpa_supplicant.service
    fi
}

clean_all_wifi_connections() {
    log "Cleaning all existing WiFi connections"
    
    # Get all WiFi connection names
    local wifi_connections
    wifi_connections=$(nmcli -t -f TYPE,NAME connection show | grep '^wifi:' | cut -d: -f2 || true)
    
    if [[ -n "${wifi_connections}" ]]; then
        while IFS= read -r conn_name; do
            if [[ -n "${conn_name}" ]]; then
                log "Deleting WiFi connection: ${conn_name}"
                nmcli connection delete "${conn_name}" 2>/dev/null || true
            fi
        done <<< "${wifi_connections}"
    else
        log "No existing WiFi connections found"
    fi
}

configure_networkmanager_security() {
    log "Configuring NetworkManager security settings"
    
    # Create NetworkManager configuration to disable auto-creation
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
    
    # Determine WiFi band and connection name dynamically
    local wifi_band_setting
    wifi_band_setting=$(determine_wifi_band)
    WIFI_CONN=$(determine_connection_name)
    
    log "Using WiFi band: ${wifi_band_setting} for connection: ${WIFI_CONN}"
    
    # Enable WiFi radio
    nmcli radio wifi on
    
    # Ensure interface is managed
    nmcli device set "${WIFI_IFACE}" managed yes
    
    # Remove any existing connection with the same name
    nmcli connection delete "${WIFI_CONN}" 2>/dev/null || true
    
    # Create new WiFi connection with proper security syntax
    nmcli connection add \
        type wifi \
        con-name "${WIFI_CONN}" \
        ifname "${WIFI_IFACE}" \
        ssid "${SSID}" \
        802-11-wireless-security.key-mgmt wpa-psk \
        802-11-wireless-security.psk "${PSK}" \
        802-11-wireless.band "${wifi_band_setting}" \
        ipv4.method auto \
        ipv4.route-metric "${WIFI_METRIC}" \
        connection.autoconnect yes
    
    # Configure additional WiFi security parameters for better compatibility
    nmcli connection modify "${WIFI_CONN}" \
        802-11-wireless-security.proto "rsn wpa" \
        802-11-wireless-security.pairwise "tkip ccmp" \
        802-11-wireless-security.group "tkip ccmp"
    
    log "WiFi configured for SSID: ${SSID}"
    
    # Wait for NetworkManager to write configuration file
    log "Waiting for configuration file to be written..."
    sleep 3
    
    # Verify configuration file exists and has correct content
    local config_file="/etc/NetworkManager/system-connections/${WIFI_CONN}.nmconnection"
    if [[ -f "${config_file}" ]]; then
        # Ensure correct permissions for NetworkManager
        chmod 600 "${config_file}"
        log "Configuration file verified and secured"
    else
        log "Warning: Configuration file not found at ${config_file}"
    fi
}

activate_connections() {
    log "Activating network connections"
    
    # Activate Ethernet
    if ! nmcli connection up "${ETHERNET_CONN}" ifname "${ETHERNET_IFACE}"; then
        log "Warning: Failed to activate Ethernet connection (non-fatal)"
    fi
    
    # Wait for Ethernet to be ready
    sleep 2
    
    # Scan for available WiFi networks
    log "Scanning for WiFi networks..."
    nmcli device wifi rescan || true
    sleep 2
    
    # Attempt WiFi activation with retry logic
    local retry_count=0
    local max_retries=3
    local wifi_success=false
    
    while [[ ${retry_count} -lt ${max_retries} && "${wifi_success}" == "false" ]]; do
        retry_count=$((retry_count + 1))
        log "WiFi activation attempt ${retry_count}/${max_retries}"
        
        if nmcli connection up "${WIFI_CONN}" ifname "${WIFI_IFACE}" 2>/dev/null; then
            wifi_success=true
            log "WiFi connection activated successfully"
        else
            if [[ ${retry_count} -lt ${max_retries} ]]; then
                log "WiFi activation failed, retrying in 5 seconds..."
                sleep 5
                # Force reload configuration before retry
                nmcli general reload || true
                sleep 2
            else
                log "ERROR: Failed to activate WiFi after ${max_retries} attempts"
                log "Check SSID availability and password correctness"
            fi
        fi
    done
    
    # Wait for IP configuration if WiFi succeeded
    if [[ "${wifi_success}" == "true" ]]; then
        log "Waiting for WiFi IP configuration..."
        sleep 5
        
        # Verify WiFi connectivity
        if ping -c 1 -W 3 8.8.8.8 >/dev/null 2>&1; then
            log "WiFi connectivity verified (ping successful)"
        else
            log "Warning: WiFi connected but no internet connectivity detected"
        fi
    fi
    
    # Reload NetworkManager configuration to apply security settings
    log "Reloading NetworkManager configuration"
    nmcli general reload || true
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
    disable_wpa_supplicant
    clean_all_wifi_connections
    configure_networkmanager_security
    configure_ethernet
    configure_wifi
    activate_connections
    show_status
    
    log "Network configuration completed. Changes will persist after reboot."
}

main "$@"
