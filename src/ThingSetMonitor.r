/*
 * Resources for ThingSet Monitor.
 */

#include "MacTypes.r"
#include "Menus.r"
#include "Dialogs.r"
#include "Processes.r"

#include "Resources.h"
#include "Icon.r"

resource 'vers' (1) {
    0x01, 0x00, release, 0x00, 0,
    "1.0",
    "1.0, ThingSet report monitor"
};

/* Menus. The enable flags hold one bit per item, item 1 in the lowest bit. */

resource 'MBAR' (rMenuBar) {
    { mApple, mFile, mEdit }
};

resource 'MENU' (mApple) {
    mApple, textMenuProc, 0x7FFFFFFD, enabled, apple,
    {
        "About ThingSet Monitor...", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain
    }
};

resource 'MENU' (mFile) {
    mFile, textMenuProc, 0x7FFFFFDB, enabled, "File",
    {
        "Connect To...", noIcon, "O", noMark, plain;
        "Close", noIcon, "W", noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Refresh", noIcon, "R", noMark, plain;
        "Clear Table", noIcon, "K", noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Quit", noIcon, "Q", noMark, plain
    }
};

/* Only Copy and Select All apply to the table; the rest are for desk accessories. */
resource 'MENU' (mEdit) {
    mEdit, textMenuProc, 0x00000088, enabled, "Edit",
    {
        "Undo", noIcon, "Z", noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Cut", noIcon, "X", noMark, plain;
        "Copy", noIcon, "C", noMark, plain;
        "Paste", noIcon, "V", noMark, plain;
        "Clear", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Select All", noIcon, "A", noMark, plain
    }
};

/* Alerts */

resource 'ALRT' (rAboutAlert) {
    { 0, 0, 120, 340 },
    rAboutAlert,
    {
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent
    },
    alertPositionMainScreen
};

resource 'DITL' (rAboutAlert) {
    {
        { 88, 260, 108, 328 },
        Button { enabled, "OK" };

        { 12, 16, 80, 328 },
        StaticText { disabled,
            "ThingSet Monitor 1.0\r\r"
            "Shows the latest values from ThingSet reports broadcast over UDP, "
            "and browses devices over TCP. Encoding and decoding by ThingSet++." }
    }
};

resource 'ALRT' (rErrorAlert) {
    { 0, 0, 110, 360 },
    rErrorAlert,
    {
        OK, visible, sound1;
        OK, visible, sound1;
        OK, visible, sound1;
        OK, visible, sound1
    },
    alertPositionMainScreen
};

resource 'DITL' (rErrorAlert) {
    {
        { 78, 280, 98, 348 },
        Button { enabled, "OK" };

        { 10, 60, 70, 348 },
        StaticText { disabled, "^0" }
    }
};

resource 'DLOG' (rConnectDialog) {
    { 0, 0, 100, 340 },
    dBoxProc,
    invisible,
    noGoAway,
    0,
    rConnectDialog,
    "Connect To",
    alertPositionMainScreen
};

resource 'DITL' (rConnectDialog) {
    {
        { 68, 252, 88, 328 },
        Button { enabled, "Connect" };

        { 68, 164, 88, 240 },
        Button { enabled, "Cancel" };

        { 36, 14, 52, 326 },
        EditText { enabled, "" };

        { 10, 12, 28, 328 },
        StaticText { disabled, "Connect to the ThingSet device at (name or IP address):" }
    }
};

resource 'SIZE' (-1) {
    reserved,
    acceptSuspendResumeEvents,
    reserved,
    canBackground,
    doesActivateOnFGSwitch,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreChildDiedEvents,
    is32BitCompatible,
    isHighLevelEventAware,
    onlyLocalHLEvents,
    notStationeryAware,
    dontUseTextEditServices,
    reserved,
    reserved,
    reserved,
    /* PowerPC code is loaded into the partition unless virtual memory is on */
    2048 * 1024,
    1536 * 1024
};
