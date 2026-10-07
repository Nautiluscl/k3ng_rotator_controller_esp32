# K3NG Rotator Controller - Enhanced Fork

> **This is an actively developed fork** of the original [K3NG Rotator Controller](https://github.com/k3ng/k3ng_rotator_controller) project by Anthony Good (K3NG). This fork extends the original firmware with significant improvements and adds a professional configuration & testing utility.

---

## 🚀 What's New in This Fork

This fork is not simply a clone—it's an evolving project that builds upon K3NG's excellent foundation with substantial enhancements:

### ✨ Professional Configuration Tool (NEW!)

A complete **Python GUI application** for configuring and testing the K3NG rotator controller **without editing C code**:

- **Visual Configuration**: Point-and-click interface for all 77 features, 60+ options, 113 pins, and 111 settings
- **Smart Validation**: Automatic dependency checking, conflict detection, and board-aware pin validation
- **Hardware Testing**: 29 automated tests for I/O verification, motor control, sensors, and calibration
- **Calibration Wizards**: Step-by-step compass and angular correction calibration with quality assessment
- **Code Generation**: Export validated configuration files ready to compile
- **Data Visualization**: Matplotlib plots for calibration analysis and error distribution

📖 **[Configuration Tool Documentation](k3ng_config_tool/README.md)** | **[User Guide](k3ng_config_tool/docs/USER_GUIDE.md)**

### 🧭 Enhanced Compass Calibration System (NEW!)

Comprehensive magnetometer calibration improvements integrated into the firmware:

- **Automatic Calibration**: Controller rotates 360° to gather calibration data
- **Manual Calibration**: User-controlled rotation with real-time data collection
- **Quality Assessment**: GOOD/SUSPECT/POOR quality indicators
- **Multi-Point Angular Correction**: Up to 8-point calibration tables for error correction across full rotation range
- **Extended Magnetometer Support**: Enhanced support for HMC5883L, QMC5883, LSM303 sensors
- **Serial Commands**: `\XMG` (automatic), `\XMGs` (manual start), `\XMGe` (manual end), `\?CAL` (status), `\?CQ` (quality check)

These improvements dramatically increase compass accuracy for homebrew and budget magnetometer installations.

### 📊 Additional Improvements

- **Enhanced Debug Logging**: Comprehensive debug output for troubleshooting
- **Improved Serial Commands**: Extended backslash command set for testing and calibration
- **Better Documentation**: Complete user guides, developer documentation, and contribution guidelines
- **Testing Infrastructure**: Automated test framework for hardware verification

---

## 📚 Original K3NG Rotator Controller

### Introduction

This is an Arduino-based rotator interface that interfaces a computer to a rotator or rotator controller, emulating the Yaesu GS-232A/B and Easycom protocols which are supported by a myriad of logging, contest, and control programs. It can be easily interfaced with commercial rotator control units. With the addition of a proper capacity power supply and several interface components such as relays, this unit could also serve as a total replacement for a rotator control unit or serve as the basis for a 100% homebrew rotation system. Several azimuth and elevation position sensors including potentiometers, rotary encoders, and I2C devices are supported. The code is very flexible, modular, and easy to read allowing intermediate and advanced experimenters and builders to customize it.

### Documentation

Full documentation for the **original K3NG project** is located [here](https://github.com/k3ng/k3ng_rotator_controller/wiki). Please read it!

**This fork's enhancements** are documented in:
- [Configuration Tool README](k3ng_config_tool/README.md)
- [User Guide](k3ng_config_tool/docs/USER_GUIDE.md)
- [Contributing Guidelines](k3ng_config_tool/CONTRIBUTING.md)

### Core Features (Original K3NG)

* Azimuth only and azimuth / elevation rotator support
* Serial interface using the standard Arduino USB port
* Control Port Protocol Support:
  * Yaesu GS-232A & GS-232B
  * Easycom
  * DCU-1 (azimuth only)
* Support for position sensors:
  * Potentiometers / Analog Voltage
  * Rotary Encoders
  * Incremental Encoders
  * Pulse Output
  * HMC5883L digital compass
  * QMC5883 digital compass
  * ADXL345 accelerometer
  * LSM303 digital compass and accelerometer
  * HH-12 / AS5045
  * A2 Absolute Encoder (under development)
* LCD display (2 or 4 rows, at least 16 columns)
* Can be interfaced with non-Yaesu rotators, including homebrew systems
* Directional indication on LCD display (North, South, North Northwest, etc.) along with degrees
* Intelligent automatic rotation (utilizes overlap on 450 degree rotators)
* Support for both 360 degree and 450 degree azimuth rotators or any rotation capability up to 719 degrees
* North Center and South Center support
* Support for any starting point (fully clockwise)
* Optional automatic azimuthal rotation slowdown feature when reaching target azimuth
* Optional rotation smooth ramp up
* Optional brake engage/disengage lines for azimuth and elevation
* Buttons for manual rotation
* Command timeout
* Timed interval rotation
* Overlap LED Indicator
* Help screen
* Speed Control, both single PWM output (compatible with Yaesu controllers) and dual PWM rotate CW and rotate CCW outputs and dual elevate up and elevate down outputs
* Variable frequency outputs
* Preset Control using either potentiometers or rotary encoders with optional preset start button
* Speed Potentiometer
* Manual Rotation Limits
* Classic 4 bit, Adafruit I2C LCD, and Yourduino.com Display Support
* Optional tenth of a degree support with Easycom protocol (i.e. 123.4 degrees)
* Park button
* Azimuth and elevation calibration tables *(enhanced in this fork)*
* Host unit and Remote unit operation for remotely located sensors using two Arduinos or ATMega chips
* Works with hamlib rotctl/rotcltd, HRD, N1MM, PST Rotator, and many more programs
* Moon and Sun Tracking
* GPS Interfacing
* Realtime Clock Interfacing

---

## 🎯 Quick Start

### Using the Configuration Tool (Recommended)

```bash
# Navigate to the configuration tool
cd k3ng_config_tool

# Install dependencies
pip install -r requirements.txt

# Launch GUI
python3 gui_main.py
```

Then:
1. Open your K3NG project directory
2. Configure features, pins, and settings visually
3. Validate configuration (auto-fix any issues)
4. Export configuration files
5. Compile and upload in Arduino IDE

**No manual .h file editing required!**

### Traditional Method

1. Download this repository
2. Edit `rotator_features.h`, `rotator_pins.h`, and `rotator_settings.h` manually
3. Compile and upload with Arduino IDE

---

## 🧪 Testing & Calibration

### Hardware Testing

Connect your Arduino and run automated tests:

```bash
cd k3ng_config_tool
python3 gui_main.py
```

Navigate to **Test** panel and run:
- I/O verification tests
- Motor control tests
- Sensor validation tests
- Calibration verification tests

Generate HTML reports with detailed results.

### Compass Calibration

#### Automatic Mode (Firmware Feature)
1. Send `\XMG` command via serial
2. Controller rotates 360° automatically
3. Check quality with `\?CQ` command
4. Save to EEPROM with `\Q` command

#### Using Configuration Tool
1. Launch **Calibrate** → **Magnetometer** wizard
2. Choose automatic or manual mode
3. Follow guided procedure
4. View quality assessment and calibration plots
5. Automatically saved to EEPROM

**Quality indicators**: GOOD (ready to use), SUSPECT (review), POOR (recalibrate)

---

## 🔧 Hardware Compatibility

**Arduino Boards Supported:**
- Arduino Mega 2560 (recommended)
- Arduino Uno
- Arduino Leonardo/Micro
- Teensy 3.x
- Arduino Due

**Configuration Tool** includes board definitions with automatic pin capability validation (PWM, interrupt, analog, digital).

---

## 📖 Documentation

### This Fork
- **[Configuration Tool Guide](k3ng_config_tool/README.md)** - Tool overview and quick start
- **[User Guide](k3ng_config_tool/docs/USER_GUIDE.md)** - Complete usage documentation
- **[Contributing](k3ng_config_tool/CONTRIBUTING.md)** - Development guidelines
- **[Phase Documentation](k3ng_config_tool/docs/)** - Implementation details

### Original K3NG Project
- **[Original Wiki](https://github.com/k3ng/k3ng_rotator_controller/wiki)** - Firmware documentation
- **[Original Repository](https://github.com/k3ng/k3ng_rotator_controller)** - K3NG's upstream repository

---

## 🤝 Contributing

This fork welcomes contributions! See **[CONTRIBUTING.md](k3ng_config_tool/CONTRIBUTING.md)** for:
- Code style guidelines
- Testing requirements
- Pull request process
- Development setup

**Areas for contribution:**
- Configuration tool enhancements
- Additional board definitions
- Calibration algorithm improvements
- Testing framework expansion
- Documentation improvements

---

## 📜 License

This project inherits the license from the original K3NG Rotator Controller project.

**Original Author:** Anthony Good (K3NG)
**Original Repository:** https://github.com/k3ng/k3ng_rotator_controller

This fork maintains the same open-source spirit while adding substantial enhancements for the ham radio community.

---

## 🙏 Acknowledgements

### Original K3NG Project

**Anthony Good, K3NG** - Original author and maintainer of the K3NG Rotator Controller project. This fork builds upon his excellent foundation.

**Contributors to original project:**
- John, W3SA - Tested on Yaesu Az/El, contributed elevation code updates
- Anthony, M0UPU - [Documented construction](http://ava.upuaut.net/?p=372), offers PC boards
- Bent, OZ1CT - Ideas, feature requests, and testing
- G4HSK - [Documented setup](http://radio.g4hsk.co.uk/2m-eme/rotator-controller/) with PstRotator and G-5500

### This Fork

**Enhancements developed with Claude Code** - Professional configuration tool, calibration improvements, and comprehensive documentation.

All trademarks mentioned are property of their respective owners.

---

## 🔗 Links

- **This Fork**: https://github.com/X9X0/k3ng_rotator_controller
- **Original K3NG Project**: https://github.com/k3ng/k3ng_rotator_controller
- **K3NG Wiki**: https://github.com/k3ng/k3ng_rotator_controller/wiki
- **Configuration Tool Docs**: [k3ng_config_tool/README.md](k3ng_config_tool/README.md)

---

## 📞 Support

### For This Fork's Enhancements
- **Issues**: [GitHub Issues](https://github.com/X9X0/k3ng_rotator_controller/issues)
- **Discussions**: Use GitHub Discussions for questions

### For Original K3NG Firmware
- **Original Support**: [K3NG Support Page](https://blog.radioartisan.com/support-for-k3ng-projects/)
- **Radio Artisan Group**: [Yahoo Group](https://groups.yahoo.com/neo/groups/radioartisan/info)

---

## 🎉 Why This Fork?

The original K3NG rotator controller is excellent firmware, but configuring it requires:
- Manual editing of multiple .h files
- Understanding C preprocessor directives
- Tracking dependencies manually
- Trial-and-error for pin conflicts
- Manual calibration procedures

**This fork adds:**
- **Professional tooling** - Configure without coding
- **Automated validation** - Catch errors before upload
- **Guided calibration** - Step-by-step wizards with quality feedback
- **Hardware testing** - Verify your setup works correctly
- **Better documentation** - Clear guides for all skill levels

The goal is to make K3NG's powerful firmware **accessible to everyone** in the ham radio community, from beginners to experts.

---

**Made with ❤️ for the ham radio community**

*DX IS!*

🤖 *Configuration tool generated with [Claude Code](https://claude.com/claude-code)*
