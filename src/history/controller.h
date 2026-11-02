// Usage history window controller: the selection by history ID, each mode's view, the range and
// the week comparison, the lines the legend hides, explicit loads with the open request they
// take, the view the frame builders read (history/view.h), and the chart laid out for its item
// with the lines the pointer picks on it (history/chart.h). Pure, like the settings edit
// controller: it talks to HistoryOps (production: history/ops.h; tests: in-memory ops) and reads
// no globals.

// What ReadSettings returns: the published settings (with this user's recording consent),
// whether the taskbar has yet to save account history IDs, the journal's storage error flag, and
// the published polls, parallel to the settings' accounts (possibly shorter).
struct HistorySettingsSnapshot {
    Settings settings;
    bool idsPendingSave = false;
    bool storageError = false;
    std::vector<AccountData> polls;
};

// What the controller reads and asks for. Production ops are stateless adapters: every request,
// load and close state is the controller's.
struct HistoryOps {
    // The latest open request's history ID (0: none), cleared.
    virtual uint64_t TakeRequest() = 0;
    // Prunes the journal to the retention, then reads its samples from `from` on; may initialize
    // or retry the storage.
    virtual HistorySnapshot ExplicitSnapshot(ULONGLONG from, int retentionDays) = 0;
    virtual HistorySettingsSnapshot ReadSettings() = 0;
    virtual void OpenSettings() = 0;
    // Unload or cancellation: nothing loads or acts any more.
    virtual bool Unloading() = 0;
    // Unix ms, and one as the user's local clock and zone (zero on failure).
    virtual ULONGLONG Now() = 0;
    virtual SYSTEMTIME LocalTime(ULONGLONG ms) = 0;

protected:
    ~HistoryOps() = default;
};

struct HistoryController {
    explicit HistoryController(HistoryOps& ops) : ops(&ops) {}

    // Ops, and the view the frame is built from (all but its theme), recomposed by each event
    HistoryOps* ops;
    HistoryUiView view;

    // Selection (0: All accounts), each mode's view, the range and the week comparison (it forces
    // 7 days; the range stays for afterwards)
    uint64_t selectedId = 0;
    HistoryView allView = HistoryView::Weekly, accountView = HistoryView::AllMeters;
    HistoryUiRange range = HistoryUiRange::Week;
    bool compare = false;

    // The last settings read and explicit load: its sorted snapshot (none before the first load)
    // and its generation (one per load), the times it covers ([readFrom, from) is the previous
    // week while comparing) and the accounts collected over them, whose rows index the snapshot
    HistorySettingsSnapshot settings;
    std::shared_ptr<const HistorySnapshot> snapshot;
    uint64_t generation = 0;
    ULONGLONG from = 0, to = 0, readFrom = 0;
    std::vector<HistoryUiAccount> accounts;

    // Navigation slots by history ID, for the session (never reused); the lines the legend hides
    // for the session (history ID, meter), whatever the selection, view or range
    std::map<uint64_t, uint32_t> slots;
    std::vector<std::pair<uint64_t, int>> hidden;

    // The chart as the last build laid it out, and the lines the pointer picks on it
    HistoryChart chart;
    std::vector<size_t> picked;

    // Set once the window closes: nothing loads or acts any more
    bool closeRequested = false;

    // The window opened: until the first load, the pill and the configured accounts come from the
    // settings alone. The open request waits for that load.
    void Open() {
        if (closeRequested || ops->Unloading()) return;
        settings = ops->ReadSettings();
        accounts = HistoryCollectAccounts(settings.settings.accounts, {}, 0, 0, [this] { return ops->Unloading(); });
        Compose();
    }

    // An explicit load (the first one, activation, refresh, a range or comparison change): takes
    // the open request, reads the settings and the journal from the range's start (and the week
    // before it while comparing) and collects the accounts over them. A listed request selects
    // its account; otherwise the selection stays while it is listed, else All accounts.
    void Load() {
        if (closeRequested || ops->Unloading()) return;
        uint64_t request = ops->TakeRequest();
        HistorySettingsSnapshot read = ops->ReadSettings();
        static constexpr int kDays[] = {1, 7, 30, 90};
        int retention = read.settings.historyRetentionDays;
        ULONGLONG now = ops->Now();
        ULONGLONG span = (compare                         ? 7 :
                          range == HistoryUiRange::All    ? ULONGLONG(std::max(1, retention)) :
                                                            ULONGLONG(kDays[int(range)])) * kHistoryDayMs;
        ULONGLONG start = now > span ? now - span : 0;
        ULONGLONG readStart = compare && start > kHistoryWeekMs ? start - kHistoryWeekMs : start;
        auto loaded = std::make_shared<HistorySnapshot>(ops->ExplicitSnapshot(readStart, retention));
        std::stable_sort(loaded->samples.begin(), loaded->samples.end(),
                         [](const HistorySample& a, const HistorySample& b) { return a.timeMs < b.timeMs; });
        std::vector<HistoryUiAccount> collected = HistoryCollectAccounts(
            read.settings.accounts, loaded->samples, readStart, now, [this] { return ops->Unloading(); });
        if (ops->Unloading()) return;
        int selected = HistoryResolveSelection(collected, selectedId, request);
        selectedId = selected >= 0 ? collected[size_t(selected)].id : 0;
        settings = std::move(read);
        snapshot = std::move(loaded);
        generation++;
        from = start;
        to = now;
        readFrom = readStart;
        accounts = std::move(collected);
        Compose();
    }

    // A control's command (history/view.h): the selection, the view and a legend chip's line
    // recompose; the range, the comparison and Refresh load; the More menu's Settings opens the
    // settings window.
    void Command(HistoryUiCommand command, int value = 0, uint64_t key = 0) {
        if (closeRequested || ops->Unloading()) return;
        switch (command) {
            case HistoryUiCommand::Select:
                selectedId = key;
                Compose();
                break;
            case HistoryUiCommand::View:
                (view.selected < 0 ? allView : accountView) =
                    HistoryView(std::clamp(value, 0, int(HistoryView::AllMeters)));
                Compose();
                break;
            case HistoryUiCommand::Range:
                range = HistoryUiRange(std::clamp(value, 0, int(HistoryUiRange::All)));
                Load();
                break;
            case HistoryUiCommand::Compare:
                compare = value != 0;
                Load();
                break;
            case HistoryUiCommand::Refresh:
                Load();
                break;
            case HistoryUiCommand::Menu:
                if (value == int(HistoryUiMenuItem::Settings)) ops->OpenSettings();
                break;
            case HistoryUiCommand::Line: {
                auto line = std::find(hidden.begin(), hidden.end(), std::pair{key, value});
                if (line != hidden.end()) hidden.erase(line);
                else hidden.push_back({key, value});
                Compose();
                break;
            }
            case HistoryUiCommand::None:
            case HistoryUiCommand::Export:
            case HistoryUiCommand::EnableRecording:
                break;
        }
    }

    // The window is closing (its close path's first step): requests stay with the opener.
    void RequestClose() { closeRequested = true; }

    // The view from the state: the accounts with their slots, recording state and latest polls
    // (a removed one named by its journal ID), the current mode's view, the settings, the pill and
    // status, then the summary cards once loaded. A selection that isn't listed is All accounts.
    void Compose() {
        const Settings& s = settings.settings;
        ULONGLONG now = ops->Now();
        MeterDisplay display = MeterDisplayFor(s, now, 1, 0);
        std::map<uint64_t, size_t> configured;
        for (size_t i = 0; i < s.accounts.size(); i++) configured.try_emplace(s.accounts[i].historyId, i);
        HistoryUiView v;
        for (const HistoryUiAccount& a : accounts) {
            HistoryUiViewAccount& account = v.accounts.emplace_back();
            account.historyId = a.id;
            account.slot = slots.try_emplace(a.id, uint32_t(slots.size())).first->second;
            account.provider = a.provider;
            account.label = a.configured ? a.label : Utf8ToWide(HistoryCsvId("account_", a.id));
            account.removed = !a.configured;
            account.recordingOff = a.configured && !a.recording;
            account.paused = a.hidden && s.skipHiddenPolling;
            // The flyout's weekly row: the latest poll's weekly used % and its staleness.
            auto index = a.configured ? configured.find(a.id) : configured.end();
            if (index != configured.end() && index->second < settings.polls.size()) {
                const AccountData& poll = settings.polls[index->second];
                account.weeklyPct = poll.winWeek.pct;
                account.stale = AccountMeterState(display, s.barLayout, s.accounts[index->second], poll, 0, 0).stale;
                account.lastPollMs = poll.lastSuccessMs;
            }
            if (a.id == selectedId) v.selected = int(v.accounts.size()) - 1;
        }
        if (v.selected < 0) selectedId = 0;
        v.view = v.selected < 0 ? allView : accountView;
        v.range = range;
        v.compare = compare;
        v.recordingEnabled = s.historyEnabled;
        v.retentionDays = s.historyRetentionDays;
        v.yellowThreshold = display.yellowThreshold;
        v.orangeThreshold = display.orangeThreshold;
        v.redThreshold = display.redThreshold;
        v.colorblind = s.colorblindMode;
        v.remaining = s.barMode == BarMode::Remaining;
        // Not saving: the loaded journal's error (before the first load, the storage flag);
        // waiting: account IDs not saved yet, or no sample in range.
        bool storageError = snapshot ? !snapshot->status.empty() : settings.storageError;
        bool waiting = settings.idsPendingSave || (snapshot && snapshot->samples.empty());
        v.pill = !s.historyEnabled ? HistoryUiPill::Off :
                 storageError      ? HistoryUiPill::NotSaving :
                 waiting           ? HistoryUiPill::Waiting : HistoryUiPill::Recording;
        v.loading = !snapshot;
        if (snapshot) {
            HistoryUiLocalTime localTime = [this](ULONGLONG ms) { return ops->LocalTime(ms); };
            v.status = L"Updated " + HistoryUiTime(to, now, localTime);
            v.storageError = snapshot->status;
            v.storageCapped = snapshot->capped;
            HistorySeriesQuery query{v.selected, v.view, from, to, readFrom, compare, s.historyEnabled,
                                     s.skipHiddenPolling, v.redThreshold};
            HistorySeriesSet set =
                HistoryBuildSeries(snapshot->samples, accounts, query, [this] { return ops->Unloading(); });
            HistoryUiSetCards(v, set, accounts, from, to, now, localTime);
            // The chart's lines: the plotted series in order, named as their cards (a removed
            // account without the collected "removed " prefix), hidden as the legend left them.
            for (size_t i = 0; i < set.series.size(); i++) {
                const HistorySeries& series = set.series[i];
                if (!series.plotted || series.account >= v.accounts.size()) continue;
                const HistoryUiViewAccount& account = v.accounts[series.account];
                std::wstring prefix = accounts[series.account].label + L" \u00B7 ";
                auto named = [&](const std::wstring& title) {
                    return title.starts_with(prefix) ? account.label + title.substr(prefix.size() - 3) : title;
                };
                HistoryUiLine& line = v.lines.emplace_back();
                line.chip = kHistoryUiChip + account.slot * 8 + uint32_t(series.meter);
                line.historyId = account.historyId;
                line.meter = series.meter;
                line.series = i;
                line.account = series.account;
                line.name = named(series.legendTitle);
                line.title = named(series.hoverTitle);
                line.color = series.color;
                line.stroke = series.stroke;
                line.low = series.low;
                line.high = series.high;
                line.hidden = std::find(hidden.begin(), hidden.end(), std::pair{line.historyId, line.meter}) != hidden.end();
            }
        }
        view = std::move(v);
    }

    // Lays the chart out in its item's rectangle at `scale` (each build that shows it; never a
    // paint): the view's lines, axes and plot, then the visible lines' periods reduced to the
    // plot's device pixels, again only when that reduction's key changed (HistoryChartKey), and the
    // time ticks. A build ended any hover: nothing is picked.
    void LayChart(UiGraphics& g, D2D1_RECT_F rect, float scale) {
        HistoryChart& c = chart;
        bool hiddenAll = std::all_of(view.lines.begin(), view.lines.end(), [](const HistoryUiLine& l) { return l.hidden; });
        c.lines = view.lines;
        c.from = from;
        c.to = to;
        c.readFrom = readFrom;
        c.compare = compare;
        c.redThreshold = view.redThreshold;
        c.message = view.lines.empty() ? (view.cards.empty() ? L"No readings in this range." :
                                              L"No percentage readings to plot. Dollar spending is shown below.") :
                    hiddenAll ? L"All lines are hidden" : L"";
        c.axes = HistoryChartAxes(c.lines);
        float plotWidth = c.plot.right - c.plot.left;
        HistoryChartLayout(g, c, rect, scale);
        HistoryChartKey key{generation, from, to, readFrom, view.selected >= 0 ? selectedId : 0, view.view, {}, c.width};
        for (const HistoryUiLine& line : c.lines) {
            if (!line.hidden) key.lines.push_back(line.chip);
        }
        bool reduce = !(key == c.key) && snapshot;
        if (reduce) {
            c.key = key;
            c.snapshot = snapshot;
            c.generation = generation;
            c.render.assign(c.lines.size(), {});
            auto cancelled = [this] { return ops->Unloading(); };
            for (size_t i = 0; i < c.lines.size(); i++) {
                const HistoryUiLine& line = c.lines[i];
                if (line.hidden || line.account >= accounts.size()) continue;
                const std::vector<size_t>& rows = accounts[line.account].rows;
                c.render[i][0] = HistoryReduceRows(snapshot->samples, rows, line.meter, from, to, c.width, cancelled);
                if (compare) {
                    c.render[i][1] = HistoryReduceRows(snapshot->samples, rows, line.meter, readFrom, from, c.width,
                                                       cancelled);
                }
            }
            // A reduction cut short is never taken for a complete one.
            if (cancelled()) c.key = {};
        }
        if (reduce || plotWidth != c.plot.right - c.plot.left) {
            c.ticks = HistoryChartTicks(from, to, c.plot.right - c.plot.left,
                                        [this](ULONGLONG ms) { return ops->LocalTime(ms); });
        }
        picked.clear();
    }

    // The pointer over the chart at `at` (item coordinates; nullopt: its hover ended): the lines
    // it picks, emphasized (true when they changed), and their readout in `readout` unless that
    // already holds it (the same key).
    bool Hover(std::optional<D2D1_POINT_2F> at, UiTooltipContent& readout, const UiPalette& palette) {
        std::vector<HistoryChartHit> hits;
        if (at && !closeRequested && !ops->Unloading()) hits = HistoryChartPick(chart, *at);
        std::vector<size_t> lines;
        for (const HistoryChartHit& hit : hits) {
            if (lines.empty() || lines.back() != hit.line) lines.push_back(hit.line);
        }
        UiTooltipContent content = HistoryChartReadout(chart, hits, palette, view.selected >= 0, ops->Now(),
                                                       [this](ULONGLONG ms) { return ops->LocalTime(ms); });
        if (content.key != readout.key) readout = std::move(content);
        bool changed = lines != picked;
        picked = std::move(lines);
        return changed;
    }
};
