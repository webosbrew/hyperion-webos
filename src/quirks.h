#pragma once

#define HAS_QUIRK(quirk_val, enum_val) ((quirk_val & enum_val) == enum_val)

enum CAPTURE_QUIRKS {
    // Use DILE_VT_CreateEx instead of DILE_VT_Create in capture_init
    QUIRK_DILE_VT_CREATE_EX = 0x1,
    // Do not freeze frame before capturing in capture_acquire_frame
    QUIRK_DILE_VT_NO_FREEZE_CAPTURE = 0x2,
    /* (WebOS 3.4) Use undocumented DUMP LOCATION 2 for
       DILE_VT_SetVideoFrameOutputDeviceDumpLocation and
       DILE_VT_SetVideoFrameOutputDeviceOutputRegion
    */
    QUIRK_ALTERNATIVE_DUMP_LOCATION = 0x4,

    // vtCapture
    // Reenables video capture using custom kernel module
    QUIRK_VTCAPTURE_FORCE_CAPTURE = 0x100
};
