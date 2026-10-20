# Library Dependencies

This project uses an Edge Impulse object detection model with the ESP32-CAM, an OLED display, and a buzzer.

## Required Software
- Arduino IDE
- ESP32 board package for Arduino
- Edge Impulse Studio

## Required Libraries
- Edge Impulse Arduino library exported from the trained object detection project
- Adafruit GFX Library
- Adafruit SSD1306
- ESP32 camera driver provided by the ESP32 platform

## Installing the Edge Impulse Model
The firmware requires the generated `objectdetection_inferencing.h` header and its associated model and SDK files.

1. Open the trained object detection project in Edge Impulse Studio.
2. Navigate to **Deployment**.
3. Select **Arduino library**.
4. Build and download the library ZIP.
5. Install the library in Arduino IDE.
6. Ensure the installed library matches the header included in `src/smart_glasses_esp32_cam.ino`.

## Hardware Components
- AI-Thinker ESP32-CAM with PSRAM
- SSD1306 OLED display (128 × 64, I2C)
- Active buzzer

## Notes
- Check the I2C address of the OLED display before uploading the firmware.
- Verify GPIO assignments and wiring before powering the hardware.
- The project is an academic prototype and is not a replacement for established mobility aids.

