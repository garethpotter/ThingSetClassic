/*
 * Base class for resizable windows showing rows of text in columns.
 */
#include "ListWindow.hpp"

#include <ControlDefinitions.h>
#include <Events.h>
#include <Fonts.h>
#include <Menus.h>
#include <OSUtils.h>
#include <Scrap.h>
#include <TextUtils.h>
#include <ToolUtils.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

static constexpr short scrollBarWidth = 16;
static constexpr short margin = 4;
static constexpr short staggerStep = 20;

static constexpr size_t noRow = (size_t)-1;

// A free function: GCC can't apply the pascal convention to a static member.
static pascal void scrollAction(ControlHandle control, short part)
{
    reinterpret_cast<ListWindow *>(GetControlReference(control))->scrollForPart(part);
}

static ControlActionUPP scrollActionUPP;

ListWindow::ListWindow()
    : _window(nullptr), _scrollBar(nullptr), _top(0), _rowHeight(12), _ascent(9), _charWidth(6), _active(false),
      _anchor(noRow), _lastClickRow(noRow), _lastClickTicks(0)
{}

ListWindow::~ListWindow()
{
    if (_window) {
        DisposeWindow(_window);
    }
}

ListWindow *ListWindow::fromWindow(WindowPtr window)
{
    // desk accessory windows have negative window kinds
    if (!window || ((WindowPeek)window)->windowKind != userKind) {
        return nullptr;
    }
    return reinterpret_cast<ListWindow *>(GetWRefCon(window));
}

bool ListWindow::create(ConstStr255Param title, short stagger)
{
    Rect screen = qd.screenBits.bounds;
    Rect bounds;
    bounds.top = screen.top + GetMBarHeight() + 24 + stagger * staggerStep;
    bounds.left = screen.left + 8 + stagger * staggerStep;
    bounds.bottom = std::max<short>(screen.bottom - 8, bounds.top + 120);
    bounds.right = std::min<short>(bounds.left + 600, screen.right - 8);

    _window = NewWindow(nullptr, &bounds, title, true, zoomDocProc, (WindowPtr)-1, true, (long)this);
    if (!_window) {
        return false;
    }
    SetPort(_window);

    TextFont(kFontIDMonaco);
    TextSize(9);
    FontInfo info;
    GetFontInfo(&info);
    _ascent = info.ascent;
    _rowHeight = info.ascent + info.descent + info.leading + 1;
    _charWidth = CharWidth('0');

    Rect rect = _window->portRect;
    _scrollBar = NewControl(_window, &rect, "\p", true, 0, 0, 0, scrollBarProc, (long)this);
    if (!_scrollBar) {
        return false;
    }

    if (!scrollActionUPP) {
        scrollActionUPP = NewControlActionUPP(scrollAction);
    }

    layout();
    return true;
}

std::string ListWindow::formatTime(unsigned long seconds)
{
    DateTimeRec time;
    SecondsToDate(seconds, &time);
    char buffer[24];
    snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", time.hour, time.minute, time.second);
    return buffer;
}

Rect ListWindow::toolbarRect() const
{
    Rect rect = _window->portRect;
    rect.bottom = rect.top + toolbarHeight();
    return rect;
}

Rect ListWindow::headerRect() const
{
    Rect rect = _window->portRect;
    rect.top += toolbarHeight();
    rect.right -= scrollBarWidth - 1;
    rect.bottom = rect.top + _rowHeight + 3;
    return rect;
}

Rect ListWindow::bodyRect() const
{
    Rect rect = _window->portRect;
    rect.top = headerRect().bottom + 1;
    rect.right -= scrollBarWidth - 1;
    // DrawGrowIcon's horizontal line marks the top of the status bar
    rect.bottom -= scrollBarWidth - 1;
    return rect;
}

Rect ListWindow::statusRect() const
{
    Rect rect = _window->portRect;
    rect.top = rect.bottom - scrollBarWidth + 2;
    rect.right -= scrollBarWidth - 1;
    return rect;
}

Rect ListWindow::rowRect(size_t index) const
{
    Rect body = bodyRect();
    Rect rect = body;
    rect.top = body.top + (short)(index - _top) * _rowHeight;
    rect.bottom = rect.top + _rowHeight;
    return rect;
}

short ListWindow::visibleRows() const
{
    Rect body = bodyRect();
    return (body.bottom - body.top) / _rowHeight;
}

void ListWindow::layout()
{
    SetPort(_window);
    Rect port = _window->portRect;
    // below the toolbar's dividing line if there is one, otherwise over the window's frame
    short top = toolbarHeight() ? port.top + toolbarHeight() - 1 : port.top - 1;
    MoveControl(_scrollBar, port.right - scrollBarWidth + 1, top);
    SizeControl(_scrollBar, scrollBarWidth, port.bottom - scrollBarWidth + 2 - top);
    if (toolbarHeight()) {
        layoutToolbar(toolbarRect());
    }
    adjustScrollBar();
}

void ListWindow::adjustScrollBar()
{
    short max = std::max<short>((short)rowCount() - visibleRows(), 0);
    SetControlMaximum(_scrollBar, max);
    if (_top > max) {
        _top = max;
        SetPort(_window);
        Rect body = bodyRect();
        InvalRect(&body);
    }
    SetControlValue(_scrollBar, _top);
}

void ListWindow::scrollTo(short row)
{
    short max = GetControlMaximum(_scrollBar);
    row = std::clamp<short>(row, 0, max);
    if (row != _top) {
        _top = row;
        SetControlValue(_scrollBar, _top);
        // draw straight away: no update events are handled while the scroll bar is tracking
        SetPort(_window);
        drawBody();
    }
}

void ListWindow::scrollForPart(short part)
{
    short page = std::max<short>(visibleRows() - 1, 1);
    switch (part) {
        case kControlUpButtonPart:
            scrollTo(_top - 1);
            break;
        case kControlDownButtonPart:
            scrollTo(_top + 1);
            break;
        case kControlPageUpPart:
            scrollTo(_top - page);
            break;
        case kControlPageDownPart:
            scrollTo(_top + page);
            break;
    }
}

void ListWindow::invalidateRow(size_t index)
{
    if (index < (size_t)_top || index >= (size_t)(_top + visibleRows() + 1)) {
        return;
    }
    SetPort(_window);
    Rect body = bodyRect();
    Rect row = rowRect(index);
    SectRect(&row, &body, &row);
    InvalRect(&row);
}

void ListWindow::rowInserted(size_t index)
{
    if (_anchor != noRow && _anchor >= index) {
        _anchor++;
    }
    _lastClickRow = noRow;

    SetPort(_window);
    if (index < (size_t)_top) {
        // keep the rows on screen where they are
        _top++;
    } else {
        Rect body = bodyRect();
        body.top = std::min(rowRect(index).top, body.bottom);
        InvalRect(&body);
    }
    adjustScrollBar();
}

void ListWindow::rowsChanged()
{
    _anchor = noRow;
    _lastClickRow = noRow;
    SetPort(_window);
    Rect body = bodyRect();
    InvalRect(&body);
    adjustScrollBar();
}

void ListWindow::setStatus(const std::string &status)
{
    if (status != _status) {
        _status = status;
        SetPort(_window);
        Rect rect = statusRect();
        InvalRect(&rect);
    }
}

void ListWindow::copySelection()
{
    size_t count = rowCount();
    bool any = false;
    for (size_t i = 0; i < count && !any; i++) {
        any = isRowSelected(i);
    }

    std::string text;
    for (size_t i = 0; i < count; i++) {
        if (!any || isRowSelected(i)) {
            text += rowText(i);
            text += '\r';
        }
    }

    if (ZeroScrap() == noErr) {
        PutScrap(text.size(), 'TEXT', text.data());
    }
}

void ListWindow::selectAll()
{
    for (size_t i = 0; i < rowCount(); i++) {
        setRowSelected(i, true);
    }
    SetPort(_window);
    drawBody();
}

void ListWindow::drawCell(const std::string &text, short left, short right, short baseline) const
{
    char buffer[256];
    short length = (short)std::min<size_t>(text.size(), sizeof(buffer));
    memcpy(buffer, text.data(), length);
    short width = right - left - _charWidth;
    if (width <= 0) {
        return;
    }
    TruncText(width, buffer, &length, smTruncEnd);
    MoveTo(left, baseline);
    DrawText(buffer, 0, length);
}

void ListWindow::drawHeader() const
{
    Rect header = headerRect();
    EraseRect(&header);

    std::vector<Column> columns;
    getColumns(bodyRect(), columns);
    short baseline = header.top + 2 + _ascent;
    TextFace(bold);
    for (const Column &column : columns) {
        drawCell(column.title, column.left, column.right, baseline);
    }
    TextFace(normal);

    MoveTo(header.left, header.bottom);
    LineTo(header.right - 1, header.bottom);
}

void ListWindow::drawRowAt(size_t index, const std::vector<Column> &columns) const
{
    Rect rect = rowRect(index);
    EraseRect(&rect);
    drawRow(index, rect, columns);
    TextFace(normal);

    if (isRowSelected(index)) {
        if (_active) {
            InvertRect(&rect);
        } else {
            FrameRect(&rect);
        }
    }
}

void ListWindow::drawBody() const
{
    Rect body = bodyRect();
    RgnHandle savedClip = NewRgn();
    GetClip(savedClip);
    ClipRect(&body);

    EraseRect(&body);
    std::vector<Column> columns;
    getColumns(body, columns);
    for (size_t i = _top; i < rowCount() && rowRect(i).top < body.bottom; i++) {
        drawRowAt(i, columns);
    }

    SetClip(savedClip);
    DisposeRgn(savedClip);
}

void ListWindow::drawStatus() const
{
    Rect rect = statusRect();
    EraseRect(&rect);
    drawCell(_status, rect.left + margin, rect.right, rect.bottom - 4);
}

void ListWindow::update()
{
    SetPort(_window);
    BeginUpdate(_window);

    EraseRect(&_window->portRect);
    if (toolbarHeight()) {
        Rect bar = toolbarRect();
        drawToolbar(bar);
        MoveTo(bar.left, bar.bottom - 1);
        LineTo(bar.right - 1, bar.bottom - 1);
    }
    drawHeader();
    drawBody();
    drawStatus();
    DrawControls(_window);
    DrawGrowIcon(_window);

    EndUpdate(_window);
}

void ListWindow::activate(bool active)
{
    _active = active;
    SetPort(_window);
    HiliteControl(_scrollBar, active ? 0 : 255);
    activated(active);
    // selected rows change between inverted and outlined, and the grow box appears or disappears
    InvalRect(&_window->portRect);
}

void ListWindow::click(Point globalWhere, const EventRecord &event)
{
    SetPort(_window);
    Point where = globalWhere;
    GlobalToLocal(&where);

    ControlHandle control;
    short part = FindControl(where, _window, &control);
    if (control == _scrollBar) {
        if (part == kControlIndicatorPart) {
            TrackControl(control, where, nullptr);
            scrollTo(GetControlValue(control));
        } else {
            TrackControl(control, where, scrollActionUPP);
        }
        return;
    }
    if (control) {
        if (TrackControl(control, where, nullptr)) {
            controlClicked(control);
        }
        return;
    }

    Rect body = bodyRect();
    if (!PtInRect(where, &body)) {
        return;
    }

    size_t count = rowCount();
    size_t index = _top + (where.v - body.top) / _rowHeight;
    if (index < count && rowClicked(index, where)) {
        _lastClickRow = noRow;
        return;
    }

    bool extend = (event.modifiers & shiftKey) != 0 && _anchor != noRow && index < count;
    for (size_t i = 0; i < count; i++) {
        setRowSelected(i, extend ? (i >= std::min(index, _anchor) && i <= std::max(index, _anchor)) : i == index);
    }
    if (!extend) {
        _anchor = index < count ? index : noRow;
    }
    drawBody();

    bool doubleClick = index < count && index == _lastClickRow && event.when - _lastClickTicks <= GetDblTime();
    _lastClickRow = doubleClick ? noRow : index;
    _lastClickTicks = event.when;
    if (doubleClick) {
        rowDoubleClicked(index);
    }
}

void ListWindow::grow(Point globalWhere)
{
    Rect limits = { 100, 320, 32767, 32767 };
    long size = GrowWindow(_window, globalWhere, &limits);
    if (size != 0) {
        SizeWindow(_window, LoWord(size), HiWord(size), true);
        InvalRect(&_window->portRect);
        layout();
    }
}

void ListWindow::zoom(Point globalWhere, short part)
{
    if (TrackBox(_window, globalWhere, part)) {
        SetPort(_window);
        EraseRect(&_window->portRect);
        ZoomWindow(_window, part, false);
        InvalRect(&_window->portRect);
        layout();
    }
}
