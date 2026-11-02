/*
 * Base class for resizable windows showing rows of text in columns, with a
 * header, a vertical scroll bar, row selection, a status bar and optionally a
 * toolbar.
 */
#pragma once

#include <Controls.h>
#include <Windows.h>

#include <string>
#include <vector>

class ListWindow
{
public:
    virtual ~ListWindow();

    WindowPtr window() const
    {
        return _window;
    }

    /// @brief Gets the ListWindow that owns a window, or null if it isn't one of ours.
    static ListWindow *fromWindow(WindowPtr window);

    bool isActive() const
    {
        return _active;
    }

    /// @brief Sets the text shown in the status bar.
    void setStatus(const std::string &status);

    /// @brief Copies the selected rows (or all rows if none are selected) as tab-separated text.
    void copySelection();
    void selectAll();

    /// @brief Called regularly from the event loop.
    virtual void idle()
    {}

    void update();
    void activate(bool active);
    void click(Point globalWhere, const EventRecord &event);
    void grow(Point globalWhere);
    void zoom(Point globalWhere, short part);

    /// @brief Scrolls for a click in the given scroll bar part; called while tracking.
    void scrollForPart(short part);

protected:
    struct Column
    {
        const char *title;
        short left;
        short right;
    };

    ListWindow();

    /// @brief Creates the window, staggered down and right by the given number of steps.
    bool create(ConstStr255Param title, short stagger);

    virtual size_t rowCount() const = 0;
    virtual bool isRowSelected(size_t index) const = 0;
    virtual void setRowSelected(size_t index, bool selected) = 0;
    /// @brief Fills in the column titles and positions for the given body rectangle.
    virtual void getColumns(const Rect &body, std::vector<Column> &columns) const = 0;
    /// @brief Draws the content of a row into its (already erased) rectangle.
    virtual void drawRow(size_t index, const Rect &rect, const std::vector<Column> &columns) const = 0;
    /// @brief Gets a row as tab-separated text for the clipboard, without a line ending.
    virtual std::string rowText(size_t index) const = 0;
    /// @brief Gives a subclass first refusal of a click in a row, e.g. on a disclosure triangle.
    /// @return True if the click was handled and the selection should be left alone.
    virtual bool rowClicked(size_t index, Point where)
    {
        (void)index;
        (void)where;
        return false;
    }
    virtual void rowDoubleClicked(size_t index)
    {
        (void)index;
    }

    /// @brief Height of a strip above the column headers for buttons; 0 for none.
    virtual short toolbarHeight() const
    {
        return 0;
    }
    /// @brief Positions controls in the toolbar after the window is created or resized.
    virtual void layoutToolbar(const Rect &bar)
    {
        (void)bar;
    }
    /// @brief Draws anything in the toolbar other than its controls.
    virtual void drawToolbar(const Rect &bar) const
    {
        (void)bar;
    }
    /// @brief Invoked when a control other than the scroll bar is clicked.
    virtual void controlClicked(ControlHandle control)
    {
        (void)control;
    }
    /// @brief Invoked when the window is activated or deactivated.
    virtual void activated(bool active)
    {
        (void)active;
    }

    Rect toolbarRect() const;
    /// @brief Formats a time as HH:MM:SS.
    static std::string formatTime(unsigned long seconds);

    /// @brief Call after inserting a row, to keep the visible rows still and redraw.
    void rowInserted(size_t index);
    /// @brief Call after rows have been rearranged, added or removed wholesale.
    void rowsChanged();
    void invalidateRow(size_t index);

    void drawCell(const std::string &text, short left, short right, short baseline) const;

    Rect bodyRect() const;
    short rowHeight() const
    {
        return _rowHeight;
    }
    short ascent() const
    {
        return _ascent;
    }
    short charWidth() const
    {
        return _charWidth;
    }

private:
    void layout();
    void adjustScrollBar();
    void scrollTo(short row);
    short visibleRows() const;
    Rect headerRect() const;
    Rect statusRect() const;
    Rect rowRect(size_t index) const;
    void drawHeader() const;
    void drawBody() const;
    void drawRowAt(size_t index, const std::vector<Column> &columns) const;
    void drawStatus() const;

    WindowPtr _window;
    ControlHandle _scrollBar;
    short _top;
    short _rowHeight;
    short _ascent;
    short _charWidth;
    bool _active;
    size_t _anchor;
    size_t _lastClickRow;
    UInt32 _lastClickTicks;
    std::string _status;
};
