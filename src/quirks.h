#pragma once

#define HAS_QUIRK(quirk_val, enum_val) ((quirk_val & enum_val) == enum_val)

enum CAPTURE_QUIRKS {
    QUIRK_DILE_VT_CREATE_EX = 0x1,
    QUIRK_DILE_VT_NO_FREEZE_CAPTURE = 0x2
};