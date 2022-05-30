# hyperion-webos

[hyperion.ng](https://github.com/hyperion-project/hyperion.ng) grabber for
webOS.

This piece of software does high-framerate/low-latency capture of contents
displayed directly on webOS TVs and transmits these frames to hyperion.ng over
the network.

[Requires root](https://rootmy.tv/).

Based on reverse-engineered internal system APIs. Still highly experimental.

If you are looking for a user-friendly UI that ships this piece of software check [PicCap](https://github.com/TBSniller/piccap). This application mainly is the underlaying service for this software.

## Known issues
* Everything is based on highly platform-specific reverse-engineered internal
  system APIs. Standard no-warranty clauses apply.

## Backends
This software uses multiple capture backends, that may work differently on some
webOS versions/hardware platforms.

Now, with unicapture, video and ui backends are seperated and only blended together if desired.

This means, UI or video capture can be turned on/off individually.

### Video capturing

| Backend        | Description                                | webOS |
|----------------|--------------------------------------------|-------|
| `libdile_vt`   | Low-level library used internally by libvt | 3.x+  |
| `libvtcapture` | High-level video capture library           | 5.x+  |

### UI capturing

| Backend        | Description                                | webOS |
|----------------|--------------------------------------------|-------|
| `libgm`        | Low-level library used internally by libvt | 3.x+  |
| `libhalgal`    | High-level video capture library           | 5.x+  |


## Running

`hyperion-webos` together with `*_backend.so` libraries need to be copied onto
the TV, eg. into `/tmp` directory.

```sh
cd /tmp
./hyperion-webos --help
./hyperion-webos -b libdile_vt -a 10.0.0.1
```

## Issues reporting

When reporting issues please include result of the following command:
```sh
grep -h -E '"(hardware_id|core_os_release|product_id|webos_manufacturing_version|board_type)"' /var/run/nyx/*
```

This contains model/hardware/region/software version, without any uniquely
identifiable information.

If a segfault/crash occurs, a crashlog file will be generated in `/var/log/reports/librdx`
or `/tmp/faultmanager/crash/`. This contains process memory dump and backtrace,
but *should* not contain any uniquely identifiable information. (though, this is not guaranteed)
