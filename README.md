# ESP/IDF HouseTrap Application


```rest
@ip = 192.168.86.49
```

## Get Info

```rest
GET http://{{ ips }}/info
```

## Shot Configuration

```rest
GET http://{{ ip }}/config/get-all
```

## Reset device

```rest
POST http://{{ ip }}/reset
```

## Delete namespace

```rest
DELETE http://{{ ip }}/config/delete-namespace
    ?namespace=mqtt
```

## Set key (MQTT Broker)
    
```rest
POST http://{{ ip }}/config/set-key
    ?namespace=mqtt
    &key=broker
content-type: application/json
{
    "type": "string",
    "value": "mqtt://192.168.86.30"
}
```

## Set key (MQTT base topic)
    

```rest
POST http://{{ ip }}/config/set-key
    ?namespace=mqtt
    &key=topic-base
content-type: application/json
{
    "type": "string",
    "value": "fh/es2/"
}
```
