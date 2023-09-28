# ESP32-BLE-Env

WiFi/MQTT reporting of BLE temperature sensors (BluecoinT, and Telink reflashed with pvvx)

## Setup

Build with WiFi and MQTT settings in config, and use `settings` to make changes as per https://github.com/revk/ESP32-RevK

## Multiple devices

The system is designed to allow one or more devices to listen for BLE temperature sensors and report to a common MQTT server. The devices all listen for the MQTT messages. The idea is that in general only one of the monitoring devices reports for each BLE sensor seen, based on whichever sees the stringest signal (`rssi`). Obviously if a device moves or is on the edge between two monitoring devices there could be some overlap/duplication, but generally anything monitoring the reports via MQTT will not have to handle multiple duplicate reports, and hence reduce clutter.

## Types of devices monitored

The devices understood are

* ELA BLE tmperation sensors
* Telink devices reflashed using PVVX reporting
* Devices using BT Home v1 reporting

(see https://pvvx.github.io/ATC_MiThermometer/TelinkMiFlasher.html for more on reflashing Telink devices)

## MQTT messages

A typical message is

```
info/BLE-Env/70041DCD4214/report {"address":"A4:C1:38:47:DE:8E","name":"A4C13847DE8E","temp":-18.60,"bat":45,"voltage":2.520,"rh":52.78,"rssi":-63}
```

* In this example `70041DCD4214` is the hostname of the monitoring device, which can be set if needed using `hostname` setting.
* The `address` is the BLE MAC, and for most of the devices monitored, this is fixed.
* The `name` is only included if passive BLE scans have a name (ELA devices) else is based on MAC.
* `temp` is the temperature (C).
* `bat` is battery percent, but note that some devices (e.g. ELA) only report when low battery.
* `voltage` is battery voltage (not reported for all formats)
* `rh` is relative humidity (%)
* `rssi` is signal strength

# Library

This is also a library used in other code to monitor such devices, and they may or may not report over MQTT.