# Build and upload

## ESP32 firmware

Tested with Arduino CLI and Arduino-ESP32 **3.2.0**, using the core's built-in libraries and camera driver. No additional Arduino libraries are required.

```sh
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.2.0 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,USBMode=hwcdc,FlashSize=16M,PSRAM=opi,PartitionScheme=huge_app" --output-dir build/firmware firmware/PortfolioAI
arduino-cli upload --port COM6 --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,USBMode=hwcdc,FlashSize=16M,PSRAM=opi,PartitionScheme=huge_app" --input-dir build/firmware firmware/PortfolioAI
```

Replace COM6 with your actual board port. Close any program using that port before upload. Use 16MB flash, OPI PSRAM and Huge APP partitioning. USB CDC on boot and hardware USB mode are required.

The prerelease archive includes the original tested application, bootloader and partition binaries, plus a merged image for an initial installation. A merged image writes the full flash address range and can overwrite stored settings. Prefer the source upload workflow for subsequent updates; keep your card configuration available.

## Portfolio DOS client

Tested with NASM **3.01**. From the repository root:

```sh
nasm -f bin portfolio/AI.ASM -o build/AI.COM
```

Create `build/` first. `portfolio/AI.COM` is the tested v0.4.14 binary. The source must reproduce it byte-for-byte. Copy only the COM and your private setup file onto the card; no DOS extender or runtime library is required.

## Local tests

Install Python dependencies with `python -m pip install -r tests/requirements.txt`.

```sh
python tests/client_uart.py portfolio/AI.COM
python tests/client_radio.py
python tools/check_secrets.py
```

On a C++17 compiler:

```sh
g++ -std=c++17 -O2 tests/dsp.cpp -o build/test-dsp
./build/test-dsp
```

On Windows, the same DSP harness builds with MSVC `/EHsc /std:c++17`. These tests exercise real client instructions and production DSP; they do not measure physical Atari serial timing or speaker sound pressure.
