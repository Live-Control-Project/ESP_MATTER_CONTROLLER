## MQTT topic:

### _preffix_/command/matter

Main topic for sending Matter device commands

## Command to reboot the device

```
{
  "actions": "reboot"
}
```

## Command to factory reset the device

```
{
    "actions": "factoryreset"
}
```

## Command to initialize OpenThread (Thread networking protocol)

```
{
    "actions": "initOpenThread"
}
```

## Command to start device pairing process

```
{
  "actions": "pairing",
  "node": 1234,               ## Node/device ID
  "method": "ble-wifi",        ## Pairing method: BLE + WiFi
  "ssid": "Wi-Fi",            ## WiFi network name
  "pwd": "password",          ## WiFi password
  "pincode": 20202021,        ## Pairing PIN code
  "discriminator": 3840       ## Device discriminator
}
```

## Command to start pairing via BLE + Thread

```
{
  "actions": "pairing",
  "node": 1234,               ## Node/device ID
  "method": "ble-thread",      ## Pairing method: BLE + Thread
  "pincode": 20202021,        ## Pairing PIN code
  "discriminator": 3840       ## Device discriminator
}
```

## Command to subscribe to device attributes

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

Console command
matter esp factoryreset

matter esp controller pairing unpair 1234

matter esp wifi connect Mikro 4455667788

matter esp ot_cli dataset init new

matter esp ot_cli dataset commit active

matter esp ot_cli dataset active -x

matter esp ot_cli ifconfig up

matter esp ot_cli thread start

matter esp controller pairing ble-thread 1234 0E080000000000010000000300000F4A0300000B35060004001FFFE00208DEAD00BEEF00CAFE0708FD000DB800A00000051000112233445566778899AABBCCDDEEFF030A4553502D6D6174746572010212340410104810E2315100AFD6BC9215A6BFAC530C0402A0F7F8 20202021 3840

matter esp controller invoke-cmd 1234 1 6 2

matter esp controller subs-attr 1234 1 6 0 0 100

matter esp controller write-attr 1234 1 6 0x4003 "{\"0:NULL\": null}"

matter esp controller read-attr 1234 0x001D 0x0000 0x0000 0x0000 0 10

matter esp controller pairing ble-wifi 1234 Mikro 4455667788 20202021 3840

idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.otbr" set-target esp32s3 build

idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32c6;sdkconfig.defaults.otbr" set-target esp32s3 build
