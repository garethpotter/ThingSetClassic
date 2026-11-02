/*
 * The monitor window: the latest value of every reported item.
 */
#include "TableWindow.hpp"

#include <Events.h>
#include <OSUtils.h>

#include <algorithm>

#include "ReportFormatter.hpp"

// Beyond this new rows are ignored, which keeps the scroll bar within range
// and the memory use bounded.
static constexpr size_t maxRows = 2000;

// How long a changed value stays bold.
static constexpr UInt32 highlightTicks = 90;

TableWindow::TableWindow() : _senderCount(0), _highlightCount(0)
{}

bool TableWindow::create(ConstStr255Param title)
{
    return ListWindow::create(title, 0);
}

bool TableWindow::setValue(InetHost host, uint32_t subsetId, std::optional<uint32_t> id,
                           const std::optional<std::string> &name, const std::string &value)
{
    bool named = name.has_value();
    uint32_t key = named ? 0 : id.value_or(0);
    static const std::string noName;
    const std::string &keyName = named ? *name : noName;

    // rows are ordered by sender, then numeric IDs, then names
    auto found = std::lower_bound(_rows.begin(), _rows.end(), 0, [&](const Row &row, int) {
        if (row.host != host) {
            return row.host < host;
        }
        if (row.named != named) {
            return !row.named;
        }
        return named ? row.name < keyName : row.id < key;
    });
    size_t index = found - _rows.begin();

    unsigned long now;
    GetDateTime(&now);

    if (found != _rows.end() && found->host == host && found->named == named
        && (named ? found->name == keyName : found->id == key))
    {
        if (found->value != value) {
            found->value = value;
            if (found->changedTicks == 0) {
                _highlightCount++;
            }
            found->changedTicks = TickCount();
        }
        found->subsetId = subsetId;
        found->updatedSeconds = now;
        invalidateRow(index);
        return true;
    }

    if (_rows.size() >= maxRows) {
        return false;
    }

    bool newSender = (index == 0 || _rows[index - 1].host != host) && (found == _rows.end() || found->host != host);
    if (newSender) {
        _senderCount++;
    }

    _rows.insert(found, Row{ host, subsetId, named, key, keyName, value, now, TickCount(), false });
    _highlightCount++;
    rowInserted(index);
    return true;
}

void TableWindow::clear()
{
    _rows.clear();
    _senderCount = 0;
    _highlightCount = 0;
    rowsChanged();
}

void TableWindow::idle()
{
    if (_highlightCount == 0) {
        return;
    }
    UInt32 now = TickCount();
    for (size_t i = 0; i < _rows.size(); i++) {
        Row &row = _rows[i];
        if (row.changedTicks != 0 && now - row.changedTicks > highlightTicks) {
            row.changedTicks = 0;
            _highlightCount--;
            invalidateRow(i);
        }
    }
}

bool TableWindow::isRowSelected(size_t index) const
{
    return _rows[index].selected;
}

void TableWindow::setRowSelected(size_t index, bool selected)
{
    _rows[index].selected = selected;
}

void TableWindow::getColumns(const Rect &body, std::vector<Column> &columns) const
{
    short cw = charWidth();
    short sender = body.left + 4;
    short subset = sender + 16 * cw;
    short id = subset + 8 * cw;
    short value = id + 13 * cw;
    short right = body.right - 4;
    short updated = std::max<short>(right - 8 * cw, value + 4 * cw);

    columns.assign({
        { "Sender", sender, subset },
        { "Subset", subset, id },
        { "ID", id, value },
        { "Value", value, updated },
        { "Updated", updated, right },
    });
}

void TableWindow::drawRow(size_t index, const Rect &rect, const std::vector<Column> &columns) const
{
    const Row &row = _rows[index];
    short baseline = rect.top + ascent();
    TextFace(row.changedTicks != 0 ? bold : normal);
    drawCell(formatAddress(row.host), columns[0].left, columns[0].right, baseline);
    drawCell(formatHex(row.subsetId), columns[1].left, columns[1].right, baseline);
    drawCell(row.named ? row.name : formatHex(row.id), columns[2].left, columns[2].right, baseline);
    drawCell(row.value, columns[3].left, columns[3].right, baseline);
    drawCell(formatTime(row.updatedSeconds), columns[4].left, columns[4].right, baseline);
}

std::string TableWindow::rowText(size_t index) const
{
    const Row &row = _rows[index];
    return formatAddress(row.host) + '\t' + formatHex(row.subsetId) + '\t' + (row.named ? row.name : formatHex(row.id))
           + '\t' + row.value + '\t' + formatTime(row.updatedSeconds);
}

void TableWindow::rowDoubleClicked(size_t index)
{
    if (onOpenSender) {
        onOpenSender(_rows[index].host);
    }
}
