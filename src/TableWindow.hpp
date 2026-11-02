/*
 * The monitor window: the latest value of every reported item, one row per
 * sender and ID, updated in place.
 */
#pragma once

#include <OpenTptInternet.h>

#include <functional>
#include <optional>

#include "ListWindow.hpp"

class TableWindow : public ListWindow
{
public:
    TableWindow();

    bool create(ConstStr255Param title);

    /// @brief Adds or updates the row for this sender and key. Exactly one of id and name is set.
    /// @return False if this is a new row and the table is full.
    bool setValue(InetHost host, uint32_t subsetId, std::optional<uint32_t> id, const std::optional<std::string> &name,
                  const std::string &value);

    size_t senderCount() const
    {
        return _senderCount;
    }

    size_t rowCount() const override
    {
        return _rows.size();
    }

    void clear();

    /// @brief Fades out change highlights.
    void idle() override;

    /// @brief Invoked when a row is double-clicked, with that row's sender.
    std::function<void(InetHost host)> onOpenSender;

protected:
    bool isRowSelected(size_t index) const override;
    void setRowSelected(size_t index, bool selected) override;
    void getColumns(const Rect &body, std::vector<Column> &columns) const override;
    void drawRow(size_t index, const Rect &rect, const std::vector<Column> &columns) const override;
    std::string rowText(size_t index) const override;
    void rowDoubleClicked(size_t index) override;

private:
    struct Row
    {
        InetHost host;
        uint32_t subsetId;
        bool named;
        uint32_t id;
        std::string name;
        std::string value;
        unsigned long updatedSeconds;
        // TickCount() when the value last changed, or 0 once the highlight has faded
        UInt32 changedTicks;
        bool selected;
    };

    std::vector<Row> _rows;
    size_t _senderCount;
    size_t _highlightCount;
};
