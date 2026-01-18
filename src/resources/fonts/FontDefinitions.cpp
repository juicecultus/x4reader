#include <Arduino.h>

// NotoSans fonts
#include "notosans/NotoSans26.h"
#include "notosans/NotoSans26Bold.h"
#include "notosans/NotoSans26BoldItalic.h"
#include "notosans/NotoSans26Italic.h"
#include "notosans/NotoSans28.h"
#include "notosans/NotoSans28Bold.h"
#include "notosans/NotoSans28BoldItalic.h"
#include "notosans/NotoSans28Italic.h"
#include "notosans/NotoSans30.h"
#include "notosans/NotoSans30Bold.h"
#include "notosans/NotoSans30BoldItalic.h"
#include "notosans/NotoSans30Italic.h"
#include "notosans/NotoSans32.h"
#include "notosans/NotoSans32Bold.h"
#include "notosans/NotoSans32BoldItalic.h"
#include "notosans/NotoSans32Italic.h"
#include "notosans/NotoSans34.h"
#include "notosans/NotoSans34Bold.h"
#include "notosans/NotoSans34BoldItalic.h"
#include "notosans/NotoSans34Italic.h"

// Bookerly fonts
#include "bookerly/Bookerly26.h"
#include "bookerly/Bookerly26Bold.h"
#include "bookerly/Bookerly26BoldItalic.h"
#include "bookerly/Bookerly26Italic.h"
#include "bookerly/Bookerly28.h"
#include "bookerly/Bookerly28Bold.h"
#include "bookerly/Bookerly28BoldItalic.h"
#include "bookerly/Bookerly28Italic.h"
#include "bookerly/Bookerly30.h"
#include "bookerly/Bookerly30Bold.h"
#include "bookerly/Bookerly30BoldItalic.h"
#include "bookerly/Bookerly30Italic.h"

// Other fonts
#include "other/MenuFontBig.h"
#include "other/MenuFontSmall.h"
#include "other/MenuHeader.h"

// Font families (group variants together)
FontFamily notoSans26Family = {
    "NotoSans26",
    &NotoSans26,           // regular
    &NotoSans26Bold,       // bold
    &NotoSans26Italic,     // italic
    &NotoSans26BoldItalic  // boldItalic
};

FontFamily notoSans28Family = {
    "NotoSans28",
    &NotoSans28,           // regular
    &NotoSans28Bold,       // bold
    &NotoSans28Italic,     // italic
    &NotoSans28BoldItalic  // boldItalic
};

FontFamily notoSans30Family = {
    "NotoSans30",
    &NotoSans30,           // regular
    &NotoSans30Bold,       // bold
    &NotoSans30Italic,     // italic
    &NotoSans30BoldItalic  // boldItalic
};

FontFamily notoSans32Family = {
    "NotoSans32",
    &NotoSans32,           // regular
    &NotoSans32Bold,       // bold
    &NotoSans32Italic,     // italic
    &NotoSans32BoldItalic  // boldItalic
};

FontFamily notoSans34Family = {
    "NotoSans34",
    &NotoSans34,           // regular
    &NotoSans34Bold,       // bold
    &NotoSans34Italic,     // italic
    &NotoSans34BoldItalic  // boldItalic
};

FontFamily bookerly26Family = {
    "Bookerly26",
    &Bookerly26,           // regular
    &Bookerly26Bold,       // bold
    &Bookerly26Italic,     // italic
    &Bookerly26BoldItalic  // boldItalic
};

FontFamily bookerly28Family = {
    "Bookerly28",
    &Bookerly28,           // regular
    &Bookerly28Bold,       // bold
    &Bookerly28Italic,     // italic
    &Bookerly28BoldItalic  // boldItalic
};

FontFamily bookerly30Family = {
    "Bookerly30",
    &Bookerly30,           // regular
    &Bookerly30Bold,       // bold
    &Bookerly30Italic,     // italic
    &Bookerly30BoldItalic  // boldItalic
};

// Other fonts
FontFamily menuFontSmallFamily = {"MenuFontSmall", &MenuFontSmall, nullptr, nullptr, nullptr};
FontFamily menuHeaderFamily = {"MenuHeader", &MenuHeader, nullptr, nullptr, nullptr};
FontFamily menuFontBigFamily = {"MenuFontBig", &MenuFontBig, nullptr, nullptr, nullptr};
