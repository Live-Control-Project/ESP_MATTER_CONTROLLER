# ESP matter Controller

This example creates a Matter Controller using the ESP Matter data model.

See the [docs](https://docs.espressif.com/projects/esp-matter/en/latest/esp32/developing.html) for more information about building and flashing the firmware.

## Additional Environment Setup

No additional setup is required.

## Controller Example

See the [docs](https://docs.espressif.com/projects/esp-matter/en/latest/esp32/developing.html#controller-example) for more information
about pairing and controling an end-device using this example

## Flash

### ESP Gateway board integrates the ESP32-S3 SoC and the ESP32-H2 RCP

### Hardware Platform

See the [docs](https://github.com/espressif/esp-thread-br#hardware-platforms) for more information about the hardware platform.

Flash ESP32-H2 RCP <a href="https://espressif.github.io/esp-launchpad/?flashConfigURL=https://live-control-project.github.io/bin/config.toml">
<img alt="Try it with ESP Launchpad" src="https://espressif.github.io/esp-launchpad/assets/try_with_launchpad.png" width="250" height="70">
</a>

![alt text](screen/esp-board_h2.jpg?raw=true)

Flash ESP32-S3 controller <a href="https://espressif.github.io/esp-launchpad/?flashConfigURL=https://live-control-project.github.io/bin/config.toml">
<img alt="Try it with ESP Launchpad" src="https://espressif.github.io/esp-launchpad/assets/try_with_launchpad.png" width="250" height="70">
</a>

![alt text](screen/esp-board_s3.jpg?raw=true)

### ESP32-C6

### Hardware Platform

See the [docs](https://www.espressif.com/en/products/socs/esp32-c6) for more information about the hardware platform.

Flash ESP32-C6 controller
<a href="https://espressif.github.io/esp-launchpad/?flashConfigURL=https://live-control-project.github.io/bin/config.toml">
<img alt="Try it with ESP Launchpad" src="https://espressif.github.io/esp-launchpad/assets/try_with_launchpad.png" width="250" height="70">
</a>

## Build Windows

![alt text](screen/Build_Windows.jpg?raw=true)

## Build Linux

ESP Gateway board

```
idf.py -D  set-target esp32s3 build
idf.py -p <PORT> erase-flash flash monitor
```

ESP32-C6

```
idf.py -D  set-target esp32c6 build
idf.py -p <PORT> erase-flash flash monitor
```

### Using

- Connect controller to Wi-Fi network with device console

```
wifi {ssid} {password}
```

- Connect controller to MQTT network with device console

```
mqtt mqtt://mqtt_server.com:1883 {prefix} -u {mqtt_user_name} -p {mqtt_user_password}
```

### MQTT API

## MQTT comand topic: {preffix}/command/matter

- Reboot controller

```
{
  "actions":"reboot"
}
```

- Matter factory reset

```
{
  "actions":"factoryreset"
}
```

- Initializing a new Thread network dataset and commit it as active one

```
{
  "actions":"initOpenThread"
}
```

- Getting operational dataset TLV-encoded string.

```

```

## MQTT comand pairing

- Pairing ble-wifi

```
{
  "actions": "pairing",
  "node": 1234,               ## Node/device ID
  "method": "ble-wifi",       ## Pairing method: BLE + WiFi
  "ssid": "Wi-Fi",            ## WiFi network name
  "pwd": "password",          ## WiFi password
  "pincode": 20202021,        ## Pairing PIN code
  "discriminator": 3840       ## Device discriminator
}
```

- Pairing ble-wifi

```
{
  "actions":"pairing",
  "node":1234,
  "method":"ble-thread",
  "pincode":20202021,
  "discriminator":3840
}
```

- Pairing onnetwork

```
{
  "actions":"pairing",
  "node":1234,
  "method":"onnetwork",
  "pincode":20202021
}
```

- Pairing code

```
{
  "actions":"pairing",
  "node":1234,
  "method":"code",
  "payload":"MT:Y.K9042C00KA0648G00"
}
```

- Pairing code-thread

```
{
  "actions":"pairing",
  "node":1234,
  "method":"code-thread",
  "payload":"setup_payload"
}
```

- Pairing code-wifi

```
{
  "actions":"pairing",
  "node":1234,
  "method":"code-wifi",
  "ssid": "Wi-Fi",
  "pwd": "password",
  "payload":"MT:Y.K9042C00KA0648G00"
}
```

- Pairing code-wifi-thread

```
{
  "actions":"pairing",
  "node":1234,
  "method":"code-wifi-thread",
  "ssid": "Wi-Fi",
  "pwd": "password",
  "payload":"setup_payload"
}
```

## MQTT control comand

- Control end-device (On/Off cluster Toggle command)

```
{
  "actions": "invoke-cmd",
  "node": 1234,
  "endpoint": 1,
  "cluster": 6,
  "command_id": 2
}
```

- Read attribute

```
{
  "actions": "read-attr",
  "node": 1234,
  "endpoint": 1,
  "cluster": 6,
  "attr": 0
}
```

- Write attribute (See the [docs](https://docs.espressif.com/projects/esp-matter/en/latest/esp32/developing.html) for more information)

```
{
  "actions": "write-attr",
  "node": 1234,
  "endpoint": 1,
  "cluster": 6,
  "attr": 0,
  "attribute_val":{{\"0:U8\": 2}}
}
```

- Command to subscribe to device attributes

```
{
  "actions": "subs-attr",
  "node": 1234,               ## Node/device ID
  "endpoint": 1,              ## Endpoint ID
  "cluster": 6,               ## Cluster ID
  "attr": 0,                  ## Attribute ID
  "min_interval": 0,          ## Minimum reporting interval (seconds)
  "max_interval": 10          ## Maximum reporting interval (seconds)
}
```

## A1 Appendix FAQs

### A1.1 Pairing Command Failed

I cannot finish the commissioning on the controller example:

- For onnetwork pairing, please make sure that the end-device and the controller are on the same IP-network.
- The controller uses the hard-code test PAA certification so the PAI and DAC on the end-device should be generated by the [test cert](https://github.com/espressif/connectedhomeip/blob/4f7669b052b16bd054227376e1bbadac85419793/credentials/test/attestation/Chip-Test-PAA-NoVID-Cert.pem) and the [test key](https://github.com/espressif/connectedhomeip/blob/4f7669b052b16bd054227376e1bbadac85419793/credentials/test/attestation/Chip-Test-PAA-NoVID-Key.pem)
- If you are still facing issues, reproduce the issue on the default example for the device and then raise an [issue](https://github.com/espressif/esp-matter/issues). Make sure to share these:
  - The complete device logs for both the devices taken over UART.
  - The esp-matter and esp-idf branch you are using.

### A1.2 Command Send Failed

I cannot send commands to the light from the controller:

- Make sure the pairing command was a success.
- Currently the cluster commands and write-attribute commands are only supported for on-off, level-control, and color-control clusters.
- The CASESession will be lost on end-device after reboot. But the controller will still send commands on the previous CASESession. The controller will release previous CASESession after the last command is timeout. So the second command will re-establish a new CASESession in this situation.
- If you are still facing issues, reproduce the issue on the default example for the device and then raise an [issue](https://github.com/espressif/esp-matter/issues). Make sure to share these:
  - The complete device logs for both the devices taken over UART.
  - The esp-matter and esp-idf branch you are using.

### A1.3 RAM optimization

- The `sdkconfig.defaults.ram_optimization` file is provided for RAM optimization. These configurations enable SPIRAM (CONFIG_SPIRAM=y) and allow the BSS segment to be placed in SPIRAM (CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY). With these configurations, [linker file](./main/linker.lf) can move move BSS segments of certain main controller libraries to SPIRAM. Build the example with the sdkconfig:

```
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.ram_optimization" set-target esp32s3 build
```

- The OTBR's sdkconfig file `sdkconfig.defaults.otbr` has RAM optimization configurations enabled by default.
- If you encounter a crash with error message: "PSRAM chip not found or not supported, or wrong PSRAM line mode", please check whether the module has SPIRAM and if the SPIRAM mode is configured correctly:
  - For 2MB SPIRAM, set `CONFIG_SPIRAM_MODE_QUAD=y`
  - For SPIRAM larger than 2MB, set `CONFIG_SPIRAM_MODE_OCT=y`
- Refer to [linker](https://docs.espressif.com/projects/esp-matter/en/latest/esp32/optimizations.html#configuration-options-to-optimize-ram-and-flash) for other options to optimize RAM and Flash.
