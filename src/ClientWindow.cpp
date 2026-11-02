/*
 * A window browsing one ThingSet device.
 */
#include "ClientWindow.hpp"

#include <ControlDefinitions.h>
#include <Events.h>
#include <OSUtils.h>

#include <algorithm>
#include <cstring>

// How long a changed value stays bold.
static constexpr UInt32 highlightTicks = 90;

// Width of each level of indentation, and of the disclosure triangle's box.
static constexpr short indentStep = 12;

static constexpr short toolbarSize = 28;
static constexpr short buttonWidth = 76;

static std::string accessText(uint8_t access)
{
    std::string text;
    if (access & 0x07) {
        text += 'r';
    }
    if (access & 0x70) {
        text += 'w';
    }
    return text.empty() ? "-" : text;
}

ClientWindow::ClientWindow(InetHost host, const std::string &label)
    : _browser(host, label), _label(label), _refreshButton(nullptr), _refreshEnabled(true), _wasBusy(true),
      _valuesSeconds(0)
{
    _browser.onStructureChanged = [this]() { rebuildRows(); };
    _browser.onValueChanged = [this](uint32_t id) { valueChanged(id); };
}

bool ClientWindow::create(short stagger)
{
    Str255 title;
    size_t length = std::min<size_t>(_label.size(), 255);
    title[0] = length;
    memcpy(&title[1], _label.data(), length);
    if (!ListWindow::create(title, stagger)) {
        return false;
    }
    rebuildRows();
    _browser.start();
    setStatus(_browser.statusText());
    return true;
}

void ClientWindow::poll()
{
    _browser.poll(TickCount());
    setStatus(_browser.statusText());

    bool busy = _browser.busy() || _browser.state() == DeviceBrowser::State::connecting;
    if (busy != _wasBusy) {
        _wasBusy = busy;
        if (!busy && _browser.state() == DeviceBrowser::State::connected) {
            GetDateTime(&_valuesSeconds);
        }
        SetPort(window());
        Rect label = toolbarLabelRect();
        InvalRect(&label);
    }
    updateRefreshButton();
}

bool ClientWindow::canRefresh() const
{
    return _browser.state() == DeviceBrowser::State::connected && !_browser.busy();
}

void ClientWindow::refresh()
{
    if (canRefresh()) {
        _browser.refresh();
    }
}

void ClientWindow::updateRefreshButton()
{
    bool enabled = isActive() && canRefresh();
    if (_refreshButton && enabled != _refreshEnabled) {
        _refreshEnabled = enabled;
        HiliteControl(_refreshButton, enabled ? 0 : 255);
    }
}

short ClientWindow::toolbarHeight() const
{
    return toolbarSize;
}

void ClientWindow::layoutToolbar(const Rect &bar)
{
    // called first while the window is being created, before there's a button
    Rect rect = { (short)(bar.top + 5), (short)(bar.left + 8), (short)(bar.top + 23), (short)(bar.left + 8 + buttonWidth) };
    if (!_refreshButton) {
        _refreshButton = NewControl(window(), &rect, "\pRefresh", true, 0, 0, 1, pushButProc, 0);
        _refreshEnabled = true;
    } else {
        MoveControl(_refreshButton, rect.left, rect.top);
    }
}

Rect ClientWindow::toolbarLabelRect() const
{
    Rect rect = toolbarRect();
    rect.left += 8 + buttonWidth + 12;
    rect.bottom -= 1;
    return rect;
}

void ClientWindow::drawToolbar(const Rect &) const
{
    std::string label;
    if (_browser.state() == DeviceBrowser::State::connected) {
        if (_wasBusy) {
            label = "Reading values...";
        } else if (_valuesSeconds != 0) {
            label = "Values as of " + formatTime(_valuesSeconds);
        }
    }
    Rect rect = toolbarLabelRect();
    drawCell(label, rect.left, rect.right, rect.top + 13 + ascent() / 2);
}

void ClientWindow::controlClicked(ControlHandle control)
{
    if (control == _refreshButton) {
        refresh();
    }
}

void ClientWindow::activated(bool)
{
    updateRefreshButton();
}

void ClientWindow::rebuildRows()
{
    _rows.clear();
    _rowIndexById.clear();
    addRows(DeviceBrowser::rootId, 0);
    for (size_t i = 0; i < _rows.size(); i++) {
        _rowIndexById[_rows[i].id] = i;
    }
    rowsChanged();
}

void ClientWindow::addRows(uint32_t id, short depth)
{
    const DeviceBrowser::Node *node = _browser.node(id);
    if (!node) {
        return;
    }
    _rows.push_back(Row{ id, depth });
    if (node->expanded) {
        for (uint32_t child : node->children) {
            addRows(child, depth + 1);
        }
    }
}

void ClientWindow::valueChanged(uint32_t id)
{
    _changedTicks[id] = TickCount();
    auto found = _rowIndexById.find(id);
    if (found != _rowIndexById.end()) {
        invalidateRow(found->second);
    }
}

void ClientWindow::idle()
{
    UInt32 now = TickCount();
    for (auto it = _changedTicks.begin(); it != _changedTicks.end();) {
        if (now - it->second > highlightTicks) {
            auto found = _rowIndexById.find(it->first);
            if (found != _rowIndexById.end()) {
                invalidateRow(found->second);
            }
            it = _changedTicks.erase(it);
        } else {
            ++it;
        }
    }
}

bool ClientWindow::isRowSelected(size_t index) const
{
    return _selected.count(_rows[index].id) != 0;
}

void ClientWindow::setRowSelected(size_t index, bool selected)
{
    if (selected) {
        _selected.insert(_rows[index].id);
    } else {
        _selected.erase(_rows[index].id);
    }
}

void ClientWindow::getColumns(const Rect &body, std::vector<Column> &columns) const
{
    short cw = charWidth();
    short name = body.left + 4;
    short right = body.right - 4;
    short access = std::max<short>(right - 4 * cw, name + 30 * cw);
    short type = std::max<short>(access - 12 * cw, name + 26 * cw);
    short value = std::min<short>(std::max<short>(name + 24 * cw, name + (type - name) * 2 / 5), type - 4 * cw);

    columns.assign({
        { "Name", name, value },
        { "Value", value, type },
        { "Type", type, access },
        { "Access", access, right },
    });
}

short ClientWindow::indent(short depth) const
{
    return depth * indentStep;
}

void ClientWindow::drawRow(size_t index, const Rect &rect, const std::vector<Column> &columns) const
{
    const Row &row = _rows[index];
    const DeviceBrowser::Node *node = _browser.node(row.id);
    if (!node) {
        return;
    }

    short left = columns[0].left + indent(row.depth);
    if (node->isExpandable()) {
        // disclosure triangle, as in the System 7 Finder's list views
        short middle = rect.top + rowHeight() / 2;
        PolyHandle triangle = OpenPoly();
        if (node->expanded) {
            MoveTo(left, middle - 2);
            LineTo(left + 8, middle - 2);
            LineTo(left + 4, middle + 2);
            LineTo(left, middle - 2);
        } else {
            MoveTo(left + 2, middle - 4);
            LineTo(left + 6, middle);
            LineTo(left + 2, middle + 4);
            LineTo(left + 2, middle - 4);
        }
        ClosePoly();
        PaintPoly(triangle);
        KillPoly(triangle);
    }

    short baseline = rect.top + ascent();
    TextFace(row.id == DeviceBrowser::rootId ? bold : normal);
    drawCell(node->name, left + indentStep, columns[0].right, baseline);

    TextFace(_changedTicks.count(row.id) ? bold : normal);
    std::string value = node->isGroup() && node->expanded && !node->childrenLoaded ? "Loading..." : node->value;
    drawCell(value, columns[1].left, columns[1].right, baseline);
    TextFace(normal);

    if (row.id != DeviceBrowser::rootId) {
        drawCell(node->type, columns[2].left, columns[2].right, baseline);
        drawCell(accessText(node->access), columns[3].left, columns[3].right, baseline);
    }
}

std::string ClientWindow::rowText(size_t index) const
{
    const DeviceBrowser::Node *node = _browser.node(_rows[index].id);
    if (!node) {
        return "";
    }
    std::string path = node->id == DeviceBrowser::rootId ? node->name : node->path;
    return path + '\t' + node->value + '\t' + node->type + '\t' + accessText(node->access);
}

void ClientWindow::toggle(size_t index)
{
    const DeviceBrowser::Node *node = _browser.node(_rows[index].id);
    if (node && node->isExpandable()) {
        _browser.setExpanded(node->id, !node->expanded);
    }
}

bool ClientWindow::rowClicked(size_t index, Point where)
{
    std::vector<Column> columns;
    getColumns(bodyRect(), columns);
    short left = columns[0].left + indent(_rows[index].depth);
    const DeviceBrowser::Node *node = _browser.node(_rows[index].id);
    if (node && node->isExpandable() && where.h >= left - 2 && where.h < left + indentStep) {
        toggle(index);
        return true;
    }
    return false;
}

void ClientWindow::rowDoubleClicked(size_t index)
{
    toggle(index);
}
