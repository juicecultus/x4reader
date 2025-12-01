# MicroReader

A minimal EPUB/TXT reader for ESP32-C3 e-ink devices.

## Features

- [x] Antialiased font rendering
- [x] TXT file reader with advanced text layout
- [x] File browser for SD card navigation
- [ ] EPUB reader

### Next Steps
- Bold/Italic Font support
- EPUB support
- Hyphenation for better text justification

## Hardware

- **Board**: ESP32-C3 (QFN32) revision v0.4
- **Chip**: ESP32-C3 (RISC-V @ 160MHz)
- **MAC Address**: 9c:13:9e:64:a6:c4
- **RAM**: 400KB SRAM
- **Flash**: 16MB
- **Display**: 4.26" E-Ink GDEQ0426T82 (800×480px, SSD1677 controller)
  - Custom SPI pins: SCLK=8, MOSI=10, CS=21, DC=4, RST=5, BUSY=6
  - Supports B&W and 4-level grayscale modes
- **Storage**: SD Card
- **Features**: WiFi, BLE

## Building

```powershell
# Build
platformio run

# Upload
platformio run -t upload

# Monitor
platformio device monitor
```

## Firmware Backup & Restore

### Backup Original Firmware

Before flashing custom firmware, back up the factory firmware:

```powershell
# Read entire 16MB flash
esptool.py --chip esp32c3 --port COM5 read_flash 0x0 0x1000000 firmware_backup.bin
```
```powershell
# Read only app0 (faster)
esptool.py --chip esp32c3 --port COM5 read_flash 0x10000 0x640000 app0_backup.bin
```


### Restore Original Firmware

To restore the backed-up firmware:

```powershell
# Write back the entire flash
esptool.py --chip esp32c3 --port COM5 write_flash 0x0 firmware_backup.bin
```

```powershell
# Write back only app0 (faster)
esptool.py --chip esp32c3 --port COM5 write_flash 0x10000 app0_backup.bin
```

**Important**: Make sure to use the correct COM port for your device.

### Switching Boot Partitions (app0/app1)

```powershell
# Backup current OTA data first
python -m esptool --port COM4 read_flash 0xE000 0x2000 otadata_backup.bin

# Flash to switch boot partition
# Boot app0
python -m esptool --port COM4 write_flash 0xE000 otadata_boot_app0.bin

# Boot app1
python -m esptool --port COM4 write_flash 0xE000 otadata_boot_app1.bin
```

### Testing Text Layout on Windows

The project includes a test suite for validating text layout, word providers, and rendering:

**Available Tests:**
- `StringWordProviderBidirectionalTest` - Validates bidirectional text tokenization
- `GreedyLayoutBidirectionalParagraphTest` - Tests greedy line breaking with bidirectional navigation
- `TextLayoutPageRenderTest` - Full page rendering and pagination tests

## Utility Scripts

### Font Generation
`scripts/generate_simplefont/` - Generate grayscale font headers from TrueType fonts
- CLI tool with character subset support
- GUI for glyph preview
- Generates bitmap preview images

```powershell
# Install dependencies first
python -m pip install -r scripts/generate_simplefont/requirements.txt

# Generate font header
python -m scripts.generate_simplefont.cli --name Font14 --size 16 --chars-file .\data\chars_input.txt --out src/resources/fonts/Font14.h --ttf .\data\YourFont.ttf

# Preview glyphs with GUI
python scripts/generate_simplefont/gui.py
```

### Image Conversion
`scripts/simple_convert_image.py` - Convert images to C++ byte arrays for display
- Supports 1-bit (B&W) and 2-bit (4-level grayscale) output
- Generates .h header files

### LUT Editor
`scripts/lut_editor.py` - Visual editor for e-ink display waveform lookup tables
- Edit voltage patterns and timing groups
- Configure display refresh settings

### Character Extraction
`scripts/extract_chars.py` - Extract unique characters from text files for font generation
