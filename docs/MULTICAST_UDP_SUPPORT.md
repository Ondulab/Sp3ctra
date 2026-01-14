# Multicast UDP Support

## Overview

The Sp3ctra application now supports both **unicast** and **multicast** UDP reception for image data streams. The system automatically detects whether the configured address is multicast and joins the appropriate multicast group.

## Configuration

Network configuration is managed via the `[network]` section in `sp3ctra.ini`:

```ini
[network]
# UDP address for data reception - can be unicast or multicast
# Unicast example: 192.168.100.1
# Multicast example: 239.100.100.100 (automatically detected and joined)
udp_address = 239.100.100.100

# UDP port for data reception (1-65535)
udp_port = 55151

# Optional: Specific network interface IP for multicast reception
# Leave empty to use INADDR_ANY (system chooses automatically)
# Example: 192.168.100.1 (your ethernet adapter IP)
multicast_interface =
```

## Features

### Automatic Multicast Detection

The system automatically detects if the configured `udp_address` is in the multicast range (224.0.0.0 - 239.255.255.255) and:

1. **Joins the multicast group** using `IP_ADD_MEMBERSHIP`
2. **Enables multicast loopback** to receive own packets if needed
3. **Logs the multicast group membership** for verification

### Unicast Mode

If the address is not multicast (e.g., `192.168.100.1`), the system operates in standard unicast mode without any multicast-specific configuration.

### Interface Selection

You can optionally specify which network interface to use for multicast reception via the `multicast_interface` parameter:

- **Empty/default**: Uses `INADDR_ANY` (system automatically selects the best interface)
- **Specific IP**: Binds to a specific interface (e.g., `192.168.100.1` for your ethernet adapter)

## Implementation Details

### Files Modified

1. **`src/config/config_loader.h`**
   - Added `udp_address`, `udp_port`, and `multicast_interface` fields to config structure

2. **`src/config/config_parser_table.h`**
   - Added `PARAM_TYPE_STRING` enum value
   - Added network parameters to configuration table

3. **`src/config/config_loader.c`**
   - Added STRING type support to config parser
   - Initialized default network values (multicast address `239.100.100.100`, port `55151`)

4. **`src/communication/network/udp.c`**
   - Added `is_multicast_address()` function to detect multicast addresses
   - Modified `udp_Init()` to read network config from global configuration
   - Implemented automatic multicast group joining with `IP_ADD_MEMBERSHIP`
   - Enabled multicast loopback with `IP_MULTICAST_LOOP`

5. **`sp3ctra.ini`**
   - Added `[network]` section with configuration parameters

## Usage Examples

### Example 1: Multicast Reception (Default)

```ini
[network]
udp_address = 239.100.100.100
udp_port = 55151
multicast_interface =
```

The application will automatically join the multicast group `239.100.100.100` on port `55151`.

### Example 2: Multicast on Specific Interface

```ini
[network]
udp_address = 239.100.100.100
udp_port = 55151
multicast_interface = 192.168.100.1
```

Forces multicast reception on the interface with IP `192.168.100.1`.

### Example 3: Unicast Mode

```ini
[network]
udp_address = 192.168.100.50
udp_port = 55151
multicast_interface =
```

Standard unicast reception on `192.168.100.50:55151`.

## Testing

To verify multicast reception, use `tcpdump`:

```bash
sudo tcpdump -n udp port 55151
```

You should see packets arriving at the multicast address:

```
IP 192.168.100.1.62510 > 239.100.100.100.55151: UDP, length 880
```

## Troubleshooting

### Application doesn't receive multicast packets

1. **Check firewall settings**: Ensure UDP port 55151 is allowed
2. **Verify multicast routing**: Use `netstat -g` (Linux) or `netstat -rn` (macOS) to check multicast groups
3. **Check interface selection**: Try specifying a specific interface via `multicast_interface`
4. **Verify multicast is enabled**: Check logs for "Successfully joined multicast group" message

### Multiple interfaces available

If you have multiple network interfaces (e.g., WiFi + Ethernet), specify the desired interface:

```ini
multicast_interface = 192.168.100.1  # Your Ethernet adapter IP
```

## Logging

The application logs multicast-related information at INFO level:

- `"Creating UDP socket for 239.100.100.100:55151"` - Socket creation
- `"Successfully joined multicast group 239.100.100.100"` - Multicast join success
- `"Unicast mode - listening on 192.168.100.1:55151"` - Unicast mode detected

## Technical Notes

- **Multicast range**: 224.0.0.0 to 239.255.255.255 (Class D addresses)
- **Loopback enabled**: Application can receive its own multicast packets
- **Thread-safe**: Configuration is read once at startup
- **Error handling**: Full error checking with descriptive log messages

## Compatibility

- **macOS**: Fully supported
- **Linux**: Fully supported (tested on Raspberry Pi 5)
- **IPv4 only**: IPv6 multicast not currently supported

## References

- RFC 1112: Host Extensions for IP Multicasting
- POSIX sockets API: `IP_ADD_MEMBERSHIP`, `IP_MULTICAST_LOOP`
