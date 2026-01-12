# ESP32-S3 FTP Server with LVGL Touchscreen

A production-ready FTP server for the ESP32-S3 (SUNTON ESP32-8048S043 board) featuring a full-color 800x480 touchscreen interface with LVGL 9.4, dual storage (internal flash + SD card), and WiFi connectivity.

## Features

- **RFC 959 Compliant FTP Server**: Full passive mode support on port 21
- **Touchscreen UI**: 800x480 RGB LCD with capacitive touch (GT911)
- **Dual Storage**: Internal flash partition + SD card hot-swap support
- **Modern Graphics**: LVGL 9.4 with hardware acceleration and tear-free rendering
- **Real-time Activity Logging**: On-screen log display with timestamps
- **Easy Configuration**: All settings via ESP-IDF menuconfig
- **Network Time Sync**: SNTP with configurable timezone
- **Secure Authentication**: Constant-time comparison for credentials

## Hardware Requirements

### Main Board
- **SUNTON ESP32-8048S043**
  - ESP32-S3 dual-core @ 240 MHz
  - 8MB PSRAM (Octal SPI)
  - 16MB Flash
  - 800x480 RGB565 LCD (16-bit parallel interface)
  - GT911 capacitive touch controller (I2C)

### Optional
- MicroSD card for external storage

## Pin Configuration

All GPIO pins are defined in [displayConfig.h](main/displayConfig.h) and match the SUNTON ESP32-8048S043 board:

- **LCD**: RGB565 parallel interface (GPIO 8-15 data, 46 PCLK, 3 HSYNC, 42 VSYNC, 41 DE)
- **Touch**: GT911 I2C (GPIO 19 SCL, GPIO 20 SDA, GPIO 38 RST, GPIO 18 INT)
- **Backlight**: PWM control (GPIO 2)
- **SD Card**: SPI (Configurable via menuconfig)

## Software Requirements

- [ESP-IDF v5.5.2+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/)
- Python 3.8+
- CMake 3.16+

## Getting Started

### 1. Clone and Setup

```bash
git clone <your-repo-url>
cd ftpServer
```

### 2. Configure the Project

```bash
idf.py menuconfig
```

Navigate to **FTP Server Configuration** and set:
- WiFi SSID and Password
- FTP Username and Password
- Timezone (e.g., "AEST-10" for Australian Eastern Standard Time)
- SD Card GPIO pins (if different from defaults)
- FTP Passive Port (default: 55555)

**Important**: Before flashing to production, clear the WiFi credentials in `sdkconfig.defaults`!

### 3. Build and Flash

```bash
idf.py build
idf.py -p COM3 flash monitor
```

Replace `COM3` with your serial port (e.g., `/dev/ttyUSB0` on Linux).

## Usage

### Starting the FTP Server

1. **Power on the board** - the display will initialize and show the FTP UI
2. **Wait for WiFi connection** - IP address will display in the top info bar
3. **Toggle the switch** - flip the center switch to ON to start the FTP server
4. **Connect via FTP client**:
   ```bash
   ftp <board-ip-address>
   # Login with credentials from menuconfig
   ```

### File System Structure

The FTP server presents a virtual root directory with two mount points:

```
/
├── data/      (Internal flash - ~1MB)
└── sd/        (SD Card - variable size)
```

**Example FTP session**:
```
ftp> ls
drwxrwxrwx   1 root  root         0 Jan 01 00:00 data
drwxrwxrwx   1 root  root         0 Jan 01 00:00 sd

ftp> cd sd
ftp> put myfile.txt
ftp> ls
-rw-rw-rw-   1 root  root      1234 Jan 12 15:30 myfile.txt
```

### SD Card Hot-Swap

The server monitors SD card availability:
- **Insert/remove** SD cards while the FTP server is running
- File operations to `/sd/` will fail gracefully if card is removed
- Status updates appear in the on-screen activity log

### UI Controls

- **Center Switch**: Toggle FTP server ON/OFF
- **Clear Log Button**: Clear the activity log display
- **Activity Log**: Scrollable log with timestamps showing:
  - File uploads/downloads
  - Directory operations
  - Connection status
  - Errors and warnings

## Architecture

### Key Components

| Component | Description |
|-----------|-------------|
| [main.cpp](main/main.cpp) | Application entry, WiFi, SNTP, task orchestration |
| [ftpServer.cpp](main/ftpServer.cpp) | RFC 959 FTP protocol implementation |
| [ftpUiScreen.cpp](main/ftpUiScreen.cpp) | LVGL 9.4 touchscreen interface |
| [displayConfig.cpp](main/displayConfig.cpp) | Hardware initialization using esp_lvgl_port |
| [filesystem.cpp](main/filesystem.cpp) | Internal flash (wear leveling) + SD card management |

### Threading Model

- **LVGL Task**: Managed by `esp_lvgl_port` (automatic tick + locking)
- **FTP Task**: FreeRTOS task running the FTP server state machine
- **Main Task**: WiFi events, SNTP sync, SD card polling

### Memory Management

- **PSRAM**: Frame buffers (800x480x2 bytes x2), LVGL widgets
- **Internal RAM**: FTP buffers (configurable), network stacks
- **Flash**: Code, partition table, internal FAT filesystem

## Configuration Options (Kconfig)

All settings are in `Kconfig.projbuild` and accessible via `idf.py menuconfig`:

### FTP Server Configuration
- `CONFIG_FTP_USER` - FTP username (default: "esp32")
- `CONFIG_FTP_PASSWORD` - FTP password (default: "esp32")
- `CONFIG_FTP_PASSIVE_PORT` - Passive mode data port (default: 55555)

### WiFi Configuration
- `CONFIG_WIFI_SSID` - WiFi network name
- `CONFIG_WIFI_PASSWORD` - WiFi password

### Time Configuration
- `CONFIG_TIMEZONE` - POSIX timezone string (e.g., "UTC+0", "EST+5")

### SD Card Configuration
- `CONFIG_SDCARD_MOSI_GPIO` - SPI MOSI pin (default: 11)
- `CONFIG_SDCARD_MISO_GPIO` - SPI MISO pin (default: 13)
- `CONFIG_SDCARD_SCLK_GPIO` - SPI CLK pin (default: 12)
- `CONFIG_SDCARD_CS_GPIO` - SPI CS pin (default: 10)

## Performance

- **FTP Transfer Speed**: ~800 KB/s (limited by WiFi + FAT filesystem)
- **Display Refresh**: 60 FPS with tear-free rendering (bounce buffer + vsync)
- **Touch Latency**: <50ms response time
- **Max Connections**: 1 FTP client (configurable)
- **RAM Usage**: ~4.5MB (including frame buffers)

## Troubleshooting

### Display shows nothing
- Check 3.3V power supply (needs stable voltage)
- Verify GPIO definitions match your board in [displayConfig.h](main/displayConfig.h)
- Monitor serial output for initialization errors

### FTP connection refused
- Verify WiFi connected (IP shown on screen)
- Check credentials in menuconfig
- Ensure FTP client uses **passive mode**
- Try: `ftp -p <ip-address>`

### SD card not detected
- Check SD card is FAT32 formatted
- Verify SPI GPIO pins in menuconfig match your board
- Some boards don't wire the card detect pin (uses polling instead)
- Monitor serial logs: `[FILESYSTEM] Mounted SD card on /sdcard`

### Touch not working
- GT911 requires I2C address 0x5D or 0x14 (set via hardware pin)
- Check I2C pullup resistors (required for I2C communication)
- Verify touch interrupt pin (GPIO 18)

### Build errors
- Ensure ESP-IDF v5.5.2 or newer: `idf.py --version`
- Clean build: `idf.py fullclean && idf.py build`
- Check all submodules: `git submodule update --init --recursive`

## Development

### Code Style
- Pure C++ (no `.c` files)
- No `extern "C"` guards in project headers (ESP-IDF components handle this)
- RAII where possible (cleanup functions for resources)
- Thread-safe UI updates using `lv_lock()` / `lv_unlock()`

### Adding Features

1. **New UI elements**: Edit [ftpUiScreen.cpp](main/ftpUiScreen.cpp)
2. **FTP commands**: Extend `ftp_cmd_table` in [ftpServer.cpp](main/ftpServer.cpp)
3. **Storage backends**: Modify [filesystem.cpp](main/filesystem.cpp)

### LVGL Port Integration

This project uses `esp_lvgl_port` for simplified LVGL integration:

```cpp
// RGB-specific configuration handles vsync automatically
const lvgl_port_display_rgb_cfg_t rgb_disp_cfg = {
    .flags = {
        .bb_mode = true,           // Bounce buffer mode
        .avoid_tearing = true      // Automatic vsync handling
    }
};
lvgl_disp = lvgl_port_add_disp_rgb(&display_config, &rgb_disp_cfg);
```

No manual flush callbacks or vsync management needed!

## Security Considerations

- **Credentials**: Change default FTP username/password before deployment
- **Network**: FTP is unencrypted - use on trusted networks only
- **Filesystem**: No sandboxing - FTP user has full access to both partitions
- **Authentication**: Uses mbedTLS constant-time comparison to prevent timing attacks

## License

This project is provided as-is under the MIT License. See [LICENSE](LICENSE) for details.

## Acknowledgments

- **ESP-IDF**: Espressif IoT Development Framework
- **LVGL**: Light and Versatile Graphics Library
- **FTP Protocol**: Based on RFC 959 specification
- **Board**: SUNTON ESP32-8048S043 (ESP32-S3 development board)

## Support

For issues and bug reports: [GitHub Issues](https://github.com/yourusername/ftpServer/issues)

---

**Built with ESP-IDF v5.5.2 | LVGL v9.4 | ESP32-S3 @ 240 MHz**
