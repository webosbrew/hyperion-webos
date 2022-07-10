#pragma once

#define HAS_QUIRK(quirk_val, enum_val) ((quirk_val & enum_val) == enum_val)

enum CAPTURE_QUIRKS {
    QUIRK_DILE_VT_CREATE_EX = 0x1,
    QUIRK_DILE_VT_NO_FREEZE_CAPTURE = 0x2,
    /* (WebOS 3.4) Use undocumented DUMP LOCATION 2 for
       DILE_VT_SetVideoFrameOutputDeviceDumpLocation and
       DILE_VT_SetVideoFrameOutputDeviceOutputRegion
    */
    QUIRK_DILE_VT_DUMP_LOCATION_2 = 0x4,
    QUIRK_VTCAPTURE_K6HP_FORCE_CAPTURE = 0x8
};