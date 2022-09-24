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
| `libgm`        | UI capture library for older TVs           | 3.x+  |
| `libhalgal`    | UI capture library for newer TVs           | 5.x+  |

### Quirks

Some TV models generally are comptabile with a specific backend, but require a slightly different routine
to work reliably.

In this case, to not need totally different binaries, we implemented *quirks*, which can be toggled on if needed.

Currently the following ones exist:

| Backend   | Quirk                   | Description                                         | Flag |
| --------- | ----------------------- | --------------------------------------------------- | ---- |
| DILE_VT   | QUIRK_DILE_VT_CREATE_EX | Use `DILE_VT_CreateEx` instead of `DILE_VT_Create`  | 0x1  |
| DILE_VT   | QUIRK_DILE_VT_NO_FREEZE_CAPTURE | Do not freeze frame before capturing (higher fps) | 0x2 |
| DILE_VT   | QUIRK_DILE_VT_DUMP_LOCATION_2   | (webOS 3.4) Use undocumented dump location 2 | 0x4  |
| VTCAPTURE   | QUIRK_VTCAPTURE_K6HP_FORCE_CAPTURE   | Use of a custom kernel module for reenable capture in special situation | 0x8  |


They can be provided in `config.json` via the `{"quirks": 0}` field or on commandline via `--quirks`.

Easiest way though -> Use PicCap GUI!

You can assemble the final quirks value by using a *bitwise-OR*,
e.g. `quirks_value = (quirk_val | quirk_val2 | quirk_val3)`.
The calculator is your friend ;)

You can find them defined here: [Source code file](https://github.com/webosbrew/hyperion-webos/blob/master/src/quirks.h)

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

## Code style / linting

To ensure a common codestyle on contributions, please ensure your submission is linted.
The linting script depends on python3 / clang-format to be installed.

Run the linting / formatter script like this:

`python lint/run-clang-format.py --extensions "c,h,cpp" --color auto --recursive --inplace true ./src`

To make a dry-run (and not auto-fix), omit the `--inplace true` parameter.