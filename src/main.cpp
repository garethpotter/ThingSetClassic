/*
 * ThingSet Monitor: shows the latest values from ThingSet reports broadcast
 * over UDP, and browses individual devices over TCP.
 *
 * Requires System 7.1 or later with Open Transport (TCP/IP).
 */
#include <AppleEvents.h>
#include <Devices.h>
#include <Dialogs.h>
#include <Events.h>
#include <Fonts.h>
#include <Gestalt.h>
#include <Menus.h>
#include <OSUtils.h>
#include <TextUtils.h>
#include <ToolUtils.h>

#include <cstdio>
#include <cstring>
#include <list>
#include <string>

#include "ClientWindow.hpp"
#include "OpenTransportSubscriptionTransport.hpp"
#include "ReportFormatter.hpp"
#include "Resources.h"
#include "TableWindow.hpp"

// Ticks to sleep in WaitNextEvent between polls of the network.
static constexpr UInt32 pollInterval = 3;

// Client windows open staggered, cycling through this many positions.
static constexpr short staggerPositions = 8;

static TableWindow *monitor;
static OpenTransportSubscriptionTransport *transport;
static std::list<ClientWindow *> clients;
static bool quitting;

// Whether the monitor should be listening, i.e. it is the active window.
static bool monitoring;

// The most recent problem worth mentioning in the monitor's status bar.
static std::string notice;

// What was last typed into the Connect To dialog.
static Str255 lastHost;

static void handleReport(const ReportSender &sender, ThingSet::ThingSetBinaryDecoder &decoder)
{
    uint32_t subsetId;
    bool full = false;
    ReportStatus status = decodeReport(sender, decoder, subsetId,
                                       [&](std::optional<uint32_t> id, const std::optional<std::string> &name,
                                           const std::string &value) {
                                           full |= !monitor->setValue(sender.host, subsetId, id, name, value);
                                       });

    std::string from = formatAddress(sender.host);
    switch (status) {
        case ReportStatus::ok:
            break;
        case ReportStatus::unsupportedType:
            notice = "Report type " + formatHex(sender.reportType) + " from " + from + " not decoded";
            break;
        case ReportStatus::malformed:
            notice = "Malformed report from " + from;
            break;
        case ReportStatus::truncated:
            notice = "Incomplete report from " + from;
            break;
    }
    if (full) {
        notice = "Table full; new items are being ignored";
    }
}

/// Listens for reports only while the monitor is the active window, so that
/// a busy network doesn't slow down everything else.
static void setMonitoring(bool on)
{
    monitoring = on;
    if (on && !transport->isOpen()) {
        transport->subscribe(handleReport);
    } else if (!on && transport->isOpen()) {
        transport->close();
    }
}

static void updateMonitorStatus()
{
    char text[100];
    if (monitoring && !transport->isOpen()) {
        snprintf(text, sizeof(text), "Could not listen on UDP port %u (error %ld)", (unsigned)transport->port(),
                 (long)transport->lastError());
    } else if (monitoring && transport->lastError() != kOTNoError) {
        snprintf(text, sizeof(text), "Open Transport error %ld", (long)transport->lastError());
    } else {
        snprintf(text, sizeof(text), "%s: %lu value%s from %lu device%s",
                 monitoring ? "Listening" : "Paused while inactive", (unsigned long)monitor->rowCount(),
                 monitor->rowCount() == 1 ? "" : "s", (unsigned long)monitor->senderCount(),
                 monitor->senderCount() == 1 ? "" : "s");
    }

    std::string status(text);
    if (!notice.empty()) {
        status += ". ";
        status += notice;
    }
    monitor->setStatus(status);
}

static void toPascal(const std::string &text, Str255 out)
{
    size_t length = std::min<size_t>(text.size(), 255);
    out[0] = length;
    memcpy(&out[1], text.data(), length);
}

static void showError(const std::string &message)
{
    Str255 text;
    toPascal(message, text);
    ParamText(text, "\p", "\p", "\p");
    StopAlert(rErrorAlert, nullptr);
}

static bool hasOpenTransportTcp()
{
    long response;
    return Gestalt(gestaltOpenTpt, &response) == noErr && (response & gestaltOpenTptPresentMask)
           && (response & gestaltOpenTptTCPPresentMask);
}

static void openClient(InetHost host, const std::string &label)
{
    for (ClientWindow *client : clients) {
        if (client->browser().host() == host) {
            SelectWindow(client->window());
            return;
        }
    }

    ClientWindow *client = new ClientWindow(host, label);
    if (!client->create(1 + clients.size() % staggerPositions)) {
        delete client;
        showError("Not enough memory to open another window.");
        return;
    }
    clients.push_back(client);
}

static void closeClient(ClientWindow *client)
{
    clients.remove(client);
    delete client;
}

/// Looks up a host name, or parses a dotted-decimal address.
static bool resolveHost(const char *name, InetHost &host)
{
    if (OTInetStringToHost(const_cast<char *>(name), &host) == kOTNoError) {
        return true;
    }

    OSStatus err;
    InetSvcRef services = OTOpenInternetServices(kDefaultInternetServicesPath, 0, &err);
    if (err != kOTNoError) {
        return false;
    }
    InetHostInfo info;
    err = OTInetStringToAddress(services, const_cast<char *>(name), &info);
    OTCloseProvider(services);
    if (err != kOTNoError || info.addrs[0] == 0) {
        return false;
    }
    host = info.addrs[0];
    return true;
}

static void connectTo()
{
    DialogPtr dialog = GetNewDialog(rConnectDialog, nullptr, (WindowPtr)-1);
    if (!dialog) {
        return;
    }
    SetDialogDefaultItem(dialog, iConnectOK);
    SetDialogCancelItem(dialog, iConnectCancel);

    short type;
    Handle field;
    Rect box;
    GetDialogItem(dialog, iConnectHost, &type, &field, &box);
    SetDialogItemText(field, lastHost);
    SelectDialogItemText(dialog, iConnectHost, 0, 32767);
    // the resource is invisible so the Dialog Manager can position it before it appears
    ShowWindow(dialog);

    short item;
    do {
        ModalDialog(nullptr, &item);
    } while (item != iConnectOK && item != iConnectCancel);

    GetDialogItemText(field, lastHost);
    DisposeDialog(dialog);
    if (item != iConnectOK || lastHost[0] == 0) {
        return;
    }

    char name[256];
    memcpy(name, &lastHost[1], lastHost[0]);
    name[lastHost[0]] = '\0';

    // looking up a name can take a while, and doesn't let anything else run
    SetCursor(*GetCursor(watchCursor));
    InetHost host;
    bool resolved = resolveHost(name, host);
    InitCursor();

    if (resolved) {
        openClient(host, name);
    } else {
        showError(std::string("Could not find the host ") + name + ".");
    }
}

// The required Apple events. Handling 'quit' is what lets the Finder quit the
// application when the Mac is shut down or restarted.

static pascal OSErr handleOpenApplication(const AppleEvent *, AppleEvent *, long)
{
    return noErr;
}

static pascal OSErr handleOpenDocuments(const AppleEvent *, AppleEvent *, long)
{
    // there are no documents to open or print
    return errAEEventNotHandled;
}

static pascal OSErr handleQuitApplication(const AppleEvent *, AppleEvent *, long)
{
    quitting = true;
    return noErr;
}

static void installAppleEventHandlers()
{
    AEInstallEventHandler(kCoreEventClass, kAEOpenApplication, NewAEEventHandlerUPP(handleOpenApplication), 0,
                          false);
    AEInstallEventHandler(kCoreEventClass, kAEOpenDocuments, NewAEEventHandlerUPP(handleOpenDocuments), 0, false);
    AEInstallEventHandler(kCoreEventClass, kAEPrintDocuments, NewAEEventHandlerUPP(handleOpenDocuments), 0, false);
    AEInstallEventHandler(kCoreEventClass, kAEQuitApplication, NewAEEventHandlerUPP(handleQuitApplication), 0,
                          false);
}

static ListWindow *frontListWindow()
{
    return ListWindow::fromWindow(FrontWindow());
}

static void closeFrontWindow()
{
    ListWindow *front = frontListWindow();
    if (front == monitor) {
        // the monitor is the main window, so closing it quits
        quitting = true;
    } else if (front) {
        closeClient(static_cast<ClientWindow *>(front));
    } else if (FrontWindow()) {
        // a desk accessory
        CloseDeskAcc(((WindowPeek)FrontWindow())->windowKind);
    }
}

static ClientWindow *frontClient()
{
    ListWindow *front = frontListWindow();
    return front && front != monitor ? static_cast<ClientWindow *>(front) : nullptr;
}

static void setItemEnabled(MenuHandle menu, short item, bool enabled)
{
    if (enabled) {
        EnableItem(menu, item);
    } else {
        DisableItem(menu, item);
    }
}

/// Enables the menu items that apply to the front window.
static void adjustMenus()
{
    MenuHandle file = GetMenuHandle(mFile);
    ClientWindow *client = frontClient();
    setItemEnabled(file, iRefresh, client && client->canRefresh());
    setItemEnabled(file, iClearTable, frontListWindow() == monitor);
}

static void handleMenu(long choice)
{
    short menu = HiWord(choice);
    short item = LoWord(choice);

    switch (menu) {
        case mApple:
            if (item == iAbout) {
                Alert(rAboutAlert, nullptr);
            } else {
                Str255 name;
                GetMenuItemText(GetMenuHandle(mApple), item, name);
                OpenDeskAcc(name);
            }
            break;
        case mFile:
            if (item == iConnect) {
                connectTo();
            } else if (item == iClose) {
                closeFrontWindow();
            } else if (item == iRefresh) {
                if (ClientWindow *client = frontClient()) {
                    client->refresh();
                }
            } else if (item == iClearTable) {
                monitor->clear();
                notice.clear();
            } else if (item == iQuit) {
                quitting = true;
            }
            break;
        case mEdit:
            if (!SystemEdit(item - 1)) {
                ListWindow *front = frontListWindow();
                if (front && item == iCopy) {
                    front->copySelection();
                } else if (front && item == iSelectAll) {
                    front->selectAll();
                }
            }
            break;
    }
    HiliteMenu(0);
}

static void handleMouseDown(const EventRecord &event)
{
    WindowPtr window;
    short part = FindWindow(event.where, &window);
    ListWindow *list = ListWindow::fromWindow(window);

    switch (part) {
        case inMenuBar:
            adjustMenus();
            handleMenu(MenuSelect(event.where));
            break;
        case inSysWindow:
            SystemClick(&event, window);
            break;
        case inDrag:
            DragWindow(window, event.where, &qd.screenBits.bounds);
            break;
        case inGoAway:
            if (TrackGoAway(window, event.where)) {
                if (list == monitor) {
                    quitting = true;
                } else if (list) {
                    closeClient(static_cast<ClientWindow *>(list));
                }
            }
            break;
        case inGrow:
            if (list) {
                list->grow(event.where);
            }
            break;
        case inZoomIn:
        case inZoomOut:
            if (list) {
                list->zoom(event.where, part);
            }
            break;
        case inContent:
            if (window != FrontWindow()) {
                SelectWindow(window);
            } else if (list) {
                list->click(event.where, event);
            }
            break;
    }
}

static void activate(ListWindow *list, bool active)
{
    list->activate(active);
    if (list == monitor) {
        setMonitoring(active);
    }
}

static void handleEvent(const EventRecord &event)
{
    switch (event.what) {
        case mouseDown:
            handleMouseDown(event);
            break;
        case keyDown:
        case autoKey:
            if (event.modifiers & cmdKey) {
                adjustMenus();
                handleMenu(MenuKey(event.message & charCodeMask));
            }
            break;
        case updateEvt:
            if (ListWindow *list = ListWindow::fromWindow((WindowPtr)event.message)) {
                list->update();
            }
            break;
        case activateEvt:
            if (ListWindow *list = ListWindow::fromWindow((WindowPtr)event.message)) {
                activate(list, (event.modifiers & activeFlag) != 0);
            }
            break;
        case kHighLevelEvent:
            AEProcessAppleEvent(&event);
            break;
        case osEvt:
            // SIZE has doesActivateOnFGSwitch, so activate/deactivate on resume/suspend ourselves
            if (((event.message >> 24) & 0xFF) == suspendResumeMessage) {
                if (ListWindow *front = frontListWindow()) {
                    activate(front, (event.message & resumeFlag) != 0);
                }
            }
            break;
    }
}

int main()
{
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(nullptr);
    InitCursor();
    MaxApplZone();
    for (int i = 0; i < 4; i++) {
        MoreMasters();
    }
    FlushEvents(everyEvent, 0);
    installAppleEventHandlers();

    SetMenuBar(GetNewMBar(rMenuBar));
    AppendResMenu(GetMenuHandle(mApple), 'DRVR');
    DrawMenuBar();

    if (!hasOpenTransportTcp()) {
        showError("ThingSet Monitor needs Open Transport with TCP/IP.");
        return 0;
    }

    if (InitOpenTransport() != kOTNoError) {
        showError("Open Transport could not be initialised.");
        return 0;
    }

    transport = new OpenTransportSubscriptionTransport();
    monitor = new TableWindow();
    if (!monitor->create("\pThingSet Monitor")) {
        showError("Not enough memory to open the monitor window.");
        CloseOpenTransport();
        return 0;
    }
    monitor->onOpenSender = [](InetHost host) { openClient(host, formatAddress(host)); };
    // listening starts when the monitor's activate event arrives

    while (!quitting) {
        EventRecord event;
        if (WaitNextEvent(everyEvent, &event, pollInterval, nullptr)) {
            handleEvent(event);
        }

        if (monitoring) {
            transport->poll();
        }
        monitor->idle();
        updateMonitorStatus();

        for (ClientWindow *client : clients) {
            client->poll();
            client->idle();
        }
    }

    while (!clients.empty()) {
        closeClient(clients.front());
    }
    delete transport;
    delete monitor;
    CloseOpenTransport();
    return 0;
}
