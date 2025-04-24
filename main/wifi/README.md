## MQTT topic: 
### *preffix*/command/matter  
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
  "metod": "ble-wifi",        ## Pairing method: BLE + WiFi  
  "ssid": "Mikro",            ## WiFi network name  
  "pwd": "4455667788",        ## WiFi password  
  "pincode": 20202021,        ## Pairing PIN code  
  "discriminator": 3840       ## Device discriminator  
}
```  

## Command to start pairing via BLE + Thread  
```
{  
  "actions": "pairing",   
  "node": 1234,               ## Node/device ID  
  "metod": "ble-thread",      ## Pairing method: BLE + Thread  
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