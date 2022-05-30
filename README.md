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
* `libvt`: may cause flickering or "No Signal" until reboot
* Everything is based on highly platform-specific reverse-engineered internal
  system APIs. Standard no-warranty clauses apply.

## Backends
This software uses multiple capture backends, that may work differently on some
webOS versions/hardware platforms.

| Backend        | Description                                                                                    | Video | UI | Framerate | webOS |
|----------------|------------------------------------------------------------------------------------------------|-------|----|-----------|-------|
| `libdile_vt`   | Low-level library used internally by libvt                                                     |   ✔   | ✘¹ | 60        | 3.x+ |
| `libvt`        | High-level video rendering library, uses OpenGL, may cause flickering/"No signal" until reboot |   ✔   | ✘¹ | ~30       | 3.x+ |
| `libvtcapture` | High-level video capture library, uses Luna bus, could possibly work without root (not now)    |   ✔   | ✔  | ~25       | 5.x+ |

¹ - UI capture could be added at some point in the future

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
