/*
 * A window browsing one ThingSet device: its schema as a tree of groups and
 * records, with values read when the user asks for them.
 */
#pragma once

#include <map>
#include <set>

#include "DeviceBrowser.hpp"
#include "ListWindow.hpp"

class ClientWindow : public ListWindow
{
public:
    ClientWindow(InetHost host, const std::string &label);

    /// @brief Opens the window and starts connecting.
    bool create(short stagger);

    const DeviceBrowser &browser() const
    {
        return _browser;
    }

    /// @brief Drives the connection.
    void poll();

    /// @brief Whether a refresh can be started now.
    bool canRefresh() const;
    /// @brief Re-reads the values of everything expanded.
    void refresh();

    /// @brief Fades out change highlights.
    void idle() override;

protected:
    size_t rowCount() const override
    {
        return _rows.size();
    }
    bool isRowSelected(size_t index) const override;
    void setRowSelected(size_t index, bool selected) override;
    void getColumns(const Rect &body, std::vector<Column> &columns) const override;
    void drawRow(size_t index, const Rect &rect, const std::vector<Column> &columns) const override;
    std::string rowText(size_t index) const override;
    bool rowClicked(size_t index, Point where) override;
    void rowDoubleClicked(size_t index) override;
    short toolbarHeight() const override;
    void layoutToolbar(const Rect &bar) override;
    void drawToolbar(const Rect &bar) const override;
    void controlClicked(ControlHandle control) override;
    void activated(bool active) override;

private:
    struct Row
    {
        uint32_t id;
        short depth;
    };

    void rebuildRows();
    void addRows(uint32_t id, short depth);
    void valueChanged(uint32_t id);
    void toggle(size_t index);
    short indent(short depth) const;
    void updateRefreshButton();
    Rect toolbarLabelRect() const;

    DeviceBrowser _browser;
    std::string _label;
    std::vector<Row> _rows;
    std::map<uint32_t, size_t> _rowIndexById;
    std::set<uint32_t> _selected;
    // TickCount() when each recently changed value changed
    std::map<uint32_t, UInt32> _changedTicks;
    ControlHandle _refreshButton;
    bool _refreshEnabled;
    bool _wasBusy;
    // when the last batch of requests finished, or 0 before the first
    unsigned long _valuesSeconds;
};
