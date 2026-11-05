// The history model (history/series.h) through its explicit inputs: grouping, accounts without
// readings, styles, labels, reduction, statistics, account collection, selection and
// cancellation.
    const std::array<std::array<std::set<int>, 4>, 5> expected{{
        {{{0}, {1, 2}, {3, 6}, {0, 1, 2, 3, 6}}},
        {{{0}, {1}, {7}, {0, 1, 7}}},
        {{{0, 4}, {1, 2, 5}, {}, {0, 1, 2, 4, 5}}},
        {{{}, {}, {3}, {3}}},
        {{{0}, {1}, {3}, {0, 1, 3}}},
    }};
    for (int selected : {-1, 0}) for (uint32_t provider = 1; provider <= 5; ++provider) {
        for (int view = 0; view < 4; ++view) {
            std::string key = "p" + std::to_string(provider) + "/v" + std::to_string(view) + "/mode=" + std::to_string(selected);
            Run("provider/view inclusion " + key, [&, provider, view, selected] {
                auto state = Model({provider}); AllLanes(state);
                state.selected = selected; state.view = HistoryView(view);
                auto built = Build(state);
                CHECK(Meters(built) == expected[provider - 1][view]);
                CHECK(built.noReadingAccounts.empty());
                for (const auto& line : built.series) {
                    CHECK(line.current.hasValue); CHECK(line.current.validSamples == 2);
                    CHECK(line.plotted == (line.meter != 6 || view == 3));
                }
                if (provider == 1 && (view == 1 || view == 3)) CHECK(Series(built, 0, 2).hoverTitle.find(L"Fable weekly") != std::wstring::npos);
                if (provider == 3 && (view == 1 || view == 3)) {
                    CHECK(Series(built, 0, 1).hoverTitle.find(L"Gemini weekly") != std::wstring::npos);
                    // Antigravity's Fable weekly lane is its Gemini Argon weekly.
                    CHECK(Series(built, 0, 2).hoverTitle == L"account-100 · Argon weekly used (%)");
                    CHECK(Series(built, 0, 2).legendTitle == (selected < 0 ? L"account-100 · Argon weekly" : L"Argon weekly"));
                    CHECK(Series(built, 0, 5).hoverTitle.find(L"Claude/GPT weekly") != std::wstring::npos);
                }
                // GitHub Copilot's extra percentage is its AI credits.
                if (provider == 4 && (view == 2 || view == 3)) {
                    CHECK(Series(built, 0, 3).hoverTitle == L"account-100 · AI credits used (%)");
                    CHECK(Series(built, 0, 3).legendTitle == (selected < 0 ? L"account-100 · AI credits" : L"AI credits"));
                }
                if (provider == 1 && (view == 2 || view == 3)) {
                    CHECK(Series(built, 0, 3).hoverTitle == L"account-100 · Extra budget used (%)");
                }
                // OpenCode Go: its 5-hour window, weekly, and its monthly in the extra percentage lane.
                if (provider == 5 && (view == 0 || view == 3)) {
                    CHECK(Series(built, 0, 0).hoverTitle == L"account-100 · 5h used (%)");
                    CHECK(Series(built, 0, 0).legendTitle == (selected < 0 ? L"account-100 · 5h" : L"5h"));
                }
                if (provider == 5 && (view == 1 || view == 3)) {
                    CHECK(Series(built, 0, 1).hoverTitle == L"account-100 · Weekly used (%)");
                }
                if (provider == 5 && (view == 2 || view == 3)) {
                    CHECK(Series(built, 0, 3).hoverTitle == L"account-100 · Monthly used (%)");
                    CHECK(Series(built, 0, 3).legendTitle == (selected < 0 ? L"account-100 · Monthly" : L"Monthly"));
                }
            });
            Run("no readings " + key, [&, provider, view, selected] {
                auto state = Model({provider});
                state.selected = selected; state.view = HistoryView(view);
                auto built = Build(state);
                CHECK(built.series.empty());
                CHECK(built.noReadingAccounts == (expected[provider - 1][view].empty() ? std::vector<size_t>{} : std::vector<size_t>{0}));
            });
            Run("previous-only data retained " + key, [&, provider, view, selected] {
                auto state = Model({provider}); state.overlay = true; state.readFrom -= kHistoryWeekMs;
                AllLanes(state, true);
                state.selected = selected; state.view = HistoryView(view);
                auto built = Build(state);
                CHECK(Meters(built) == expected[provider - 1][view]);
                CHECK(built.noReadingAccounts.empty());
                for (const auto& line : built.series) {
                    CHECK(!line.current.hasValue); CHECK(line.previous.hasValue);
                    CHECK(Reduce(state, line, 0).points.empty());
                    CHECK(Reduce(state, line, 1).points.size() == 2);
                }
                state.overlay = false; state.readFrom = state.from;
                CHECK(Build(state).series.empty());
            });
        }
    }
    for (int selected : {-1, 0}) for (int view = 0; view < 4; ++view) {
        Run("no provider/v" + std::to_string(view) + "/mode=" + std::to_string(selected), [=] {
            auto state = Model({0}); AllLanes(state);
            state.view = HistoryView(view); state.selected = selected;
            auto built = Build(state);
            CHECK(built.series.empty()); CHECK(built.noReadingAccounts.empty());
        });
    }
    // A later version's provider: every lane it recorded in range, in each view's lanes, under
    // the lanes' generic names.
    const std::array<std::set<int>, 4> viewLanes{{{0, 4}, {1, 2, 5}, {3, 6, 7}, {0, 1, 2, 3, 4, 5, 6, 7}}};
    for (int selected : {-1, 0}) for (uint32_t provider : {6U, UINT_MAX}) for (int view = 0; view < 4; ++view) {
        std::string key = std::to_string(provider) + "/v" + std::to_string(view) + "/mode=" + std::to_string(selected);
        Run("later provider, every lane " + key, [&, provider, view, selected] {
            auto state = Model({provider}); AllLanes(state);
            state.view = HistoryView(view); state.selected = selected;
            auto built = Build(state);
            CHECK(Meters(built) == viewLanes[view]);
            CHECK(built.noReadingAccounts.empty());
            for (const auto& line : built.series) {
                CHECK(line.current.validSamples == 2);
                CHECK(line.plotted == (line.meter != 6 || view == 3));
            }
            if (view == 0 || view == 3) CHECK(Series(built, 0, 0).hoverTitle == L"account-100 · 5h used (%)");
            if (view == 1 || view == 3) CHECK(Series(built, 0, 2).hoverTitle == L"account-100 · Fable weekly used (%)");
            if (view == 2 || view == 3) CHECK(Series(built, 0, 3).hoverTitle == L"account-100 · Extra budget used (%)");
        });
        Run("later provider, one lane " + key, [&, provider, view, selected] {
            auto state = Model({provider});
            AddRow(state, 0, state.from + 1000, {{3, 40}});
            AddRow(state, 0, state.from + 61000, {{3, 45}});
            state.view = HistoryView(view); state.selected = selected;
            auto built = Build(state);
            CHECK(Meters(built) == (view == 2 || view == 3 ? std::set<int>{3} : std::set<int>{}));
            CHECK(built.noReadingAccounts.empty());
        });
        Run("later provider, lanes outside the range " + key, [&, provider, view, selected] {
            auto state = Model({provider});
            AddRow(state, 0, state.readFrom - 1, {{0, 10}, {1, 20}, {3, 40}});
            AddRow(state, 0, state.to, {{0, 15}, {1, 25}, {3, 45}});
            AddRow(state, 0, state.from + 1000, {});
            state.view = HistoryView(view); state.selected = selected;
            auto built = Build(state);
            CHECK(built.series.empty()); CHECK(built.noReadingAccounts.empty());
        });
    }

    // Explicit flag cases: no second implementation of the production meter mask.
    const std::array<std::pair<bool, bool>, 4> weeklyFlags{{{false, false}, {true, false}, {false, true}, {true, true}}};
    const std::array<std::set<int>, 4> weeklyMeters{{{}, {1}, {2}, {1, 2}}};
    for (int selected : {-1, 0}) for (size_t flags = 0; flags < weeklyFlags.size(); ++flags) {
        for (int readings : {0, 1, 2}) Run("Anthropic weekly flags=" + std::to_string(flags) +
                "/readings=" + std::to_string(readings) + "/mode=" + std::to_string(selected), [&, selected, flags, readings] {
            auto state = Model({1}); state.selected = selected;
            state.accounts[0].showWeekly = weeklyFlags[flags].first;
            state.accounts[0].showFableWeekly = weeklyFlags[flags].second;
            state.overlay = readings == 2;
            if (state.overlay) state.readFrom -= kHistoryWeekMs;
            if (readings) AllLanes(state, state.overlay);
            state.view = HistoryView::Weekly;
            auto built = Build(state);
            CHECK(Meters(built) == (readings ? weeklyMeters[flags] : std::set<int>{}));
            CHECK(built.noReadingAccounts.size() == (!readings && !weeklyMeters[flags].empty() ? 1 : 0));
            for (const auto& line : built.series) {
                CHECK(line.current.hasValue == (readings == 1)); CHECK(line.previous.hasValue == (readings == 2));
                CHECK(Reduce(state, line, 0).points.size() == (readings == 1 ? 2 : 0));
                CHECK(Reduce(state, line, 1).points.size() == (readings == 2 ? 2 : 0));
            }
            // Neither Short-term, Extra usage nor All meters honors the weekly display flags.
            for (int view : {0, 2, 3}) {
                state.view = HistoryView(view);
                built = Build(state);
                CHECK(Meters(built) == (readings ? expected[0][view] : std::set<int>{}));
                CHECK(built.noReadingAccounts.size() == (readings ? 0 : 1));
                if (readings && view == 3) {
                    CHECK(Series(built, 0, 6).plotted);
                    CHECK(Reduce(state, Series(built, 0, 6), state.overlay ? 1 : 0).points.size() == 2);
                }
            }
            if (state.overlay) {
                state.overlay = false; state.readFrom = state.from;
                state.view = HistoryView::Weekly;
                built = Build(state);
                CHECK(built.series.empty());
                CHECK(built.noReadingAccounts.size() == (weeklyMeters[flags].empty() ? 0 : 1));
            }
        });
    }

    for (int selected : {-1, 0}) for (bool previous : {false, true}) {
        Run("disabled weekly readings do not fill enabled meter/mode=" + std::to_string(selected) +
                "/previous=" + std::to_string(previous), [=] {
            auto state = Model({1}); state.selected = selected; state.overlay = previous;
            if (previous) state.readFrom -= kHistoryWeekMs;
            state.accounts[0].showFableWeekly = false;
            AddRow(state, 0, state.readFrom + 1000, {{2, 30}});
            state.view = HistoryView::Weekly;
            auto built = Build(state);
            CHECK(built.series.empty()); CHECK(built.noReadingAccounts == std::vector<size_t>{0});
            state.accounts[0].showWeekly = false; state.accounts[0].showFableWeekly = true;
            built = Build(state);
            CHECK(Meters(built) == std::set<int>{2}); CHECK(built.noReadingAccounts.empty());
            CHECK(Series(built, 0, 2).current.hasValue == !previous);
            CHECK(Series(built, 0, 2).previous.hasValue == previous);
        });
    }

    Run("mixed weekly flags stay account-local; removed and other providers ignore them", [&] {
        auto state = Model({1, 1, 1, 1, 1, 2, 3}); AllLanes(state);
        for (auto& account : state.accounts) account.showWeekly = account.showFableWeekly = false;
        for (size_t i = 0; i < weeklyFlags.size(); ++i) {
            state.accounts[i].showWeekly = weeklyFlags[i].first;
            state.accounts[i].showFableWeekly = weeklyFlags[i].second;
        }
        state.accounts[4].configured = false;
        const std::array<std::set<int>, 7> mixedMeters{{{}, {1}, {2}, {1, 2}, {1, 2}, {1}, {1, 2, 5}}};
        state.view = HistoryView::Weekly;
        auto built = Build(state);
        CHECK(built.series.size() == 10); CHECK(built.noReadingAccounts.empty());
        for (size_t i = 0; i < state.accounts.size(); ++i) CHECK(Meters(built, i) == mixedMeters[i]);
        for (size_t i = 0; i < state.accounts.size(); ++i) {
            state.selected = int(i); built = Build(state);
            CHECK(Meters(built, i) == mixedMeters[i]); CHECK(built.noReadingAccounts.empty());
            CHECK(built.series.size() == mixedMeters[i].size());
        }
        // A disabled account must not leave a no-readings entry behind either.
        for (auto& account : state.accounts) account.rows.clear();
        state.selected = -1; built = Build(state);
        CHECK(built.series.empty()); CHECK((built.noReadingAccounts == std::vector<size_t>{1, 2, 3, 4, 5, 6}));
        for (size_t i = 0; i < state.accounts.size(); ++i) {
            state.selected = int(i); built = Build(state);
            CHECK(built.series.empty());
            CHECK(built.noReadingAccounts == (i == 0 ? std::vector<size_t>{} : std::vector<size_t>{i}));
        }
    });

    Run("Extra USD is detail-only; All meters plots it", [] {
        auto state = Model(); state.overlay = true; state.readFrom -= kHistoryWeekMs;
        AllLanes(state, true); AllLanes(state);
        state.view = HistoryView::ExtraUsage;
        auto built = Build(state);
        CHECK(built.series.size() == 3); CHECK(built.noReadingAccounts.empty());
        const auto usd = Series(built, 0, 6);
        CHECK(!usd.plotted); CHECK(usd.current.latest == 15.5); CHECK(usd.current.peak == 15.5);
        CHECK(usd.previous.hasValue);
        CHECK(Series(built, 0, 3).plotted && Series(built, 1, 7).plotted);
        state.selected = 0; state.view = HistoryView::AllMeters;
        built = Build(state);
        CHECK(Series(built, 0, 6).plotted);
        state.view = HistoryView::ExtraUsage;
        built = Build(state);
        CHECK(!Series(built, 0, 6).plotted);
    });

    for (bool previous : {false, true}) Run(std::string("USD-only account not empty/") + (previous ? "previous" : "current"), [=] {
        auto state = Model({1}); state.overlay = previous;
        if (previous) state.readFrom -= kHistoryWeekMs;
        AddRow(state, 0, state.readFrom + 1000, {{6, 30}});
        AddRow(state, 0, state.readFrom + 61000, {{6, 5}});
        state.view = HistoryView::ExtraUsage;
        auto built = Build(state);
        CHECK(built.series.size() == 1); CHECK(built.noReadingAccounts.empty());
        const auto& usd = built.series[0]; const auto& stats = previous ? usd.previous : usd.current;
        CHECK(!usd.plotted); CHECK(stats.latest == 5); CHECK(stats.peak == 30);
    });

    Run("all-account colors/widths stable when views omit preceding accounts", [] {
        auto state = Model({3, 1, 2, 1, 3, 2, 1, 2, 3, 1}); AllLanes(state);
        const COLORREF palette[] = {RGB(0,80,230), RGB(220,100,0), RGB(0,140,40), RGB(200,0,160),
            RGB(115,45,200), RGB(0,140,170), RGB(35,35,35), RGB(220,25,35)};
        HistorySeriesSet built;
        for (auto view : {HistoryView::Weekly, HistoryView::ExtraUsage, HistoryView::ShortTerm, HistoryView::AllMeters, HistoryView::Weekly}) {
            state.view = view;
            built = Build(state);
            for (const auto& series : built.series) {
                bool scoped = series.meter == 2 || series.meter == 4 || series.meter == 5;
                COLORREF color = palette[series.account % 8];
                if (scoped) color = RGB((GetRValue(color)*3+255)/4, (GetGValue(color)*3+255)/4, (GetBValue(color)*3+255)/4);
                CHECK(series.color == color); CHECK(series.stroke == (scoped ? 2 : 3));
            }
        }
        auto weekly = built.series;
        built = Build(state);
        CHECK(built.series.size() == weekly.size());
        for (size_t i = 0; i < weekly.size(); ++i) {
            CHECK(built.series[i].color == weekly[i].color); CHECK(built.series[i].stroke == weekly[i].stroke);
        }
    });

    for (int account = 0; account < 3; ++account) Run("single-account meter styles stable/" + std::to_string(account), [=] {
        auto state = Model(); AllLanes(state);
        state.selected = account; state.view = HistoryView::AllMeters;
        std::map<int, std::pair<COLORREF, int>> styles;
        auto built = Build(state);
        for (const auto& line : built.series) styles[line.meter] = {line.color, line.stroke};
        for (auto view : {HistoryView::Weekly, HistoryView::ExtraUsage, HistoryView::ShortTerm, HistoryView::AllMeters}) {
            state.view = view;
            built = Build(state);
            for (const auto& line : built.series) CHECK((styles.at(line.meter) == std::pair{line.color, line.stroke}));
        }
    });

    const std::vector<std::pair<std::vector<ULONGLONG>, std::wstring>> durations{
        {{5*hour, 5*hour}, L"5h"}, {{90*60000, 90*60000}, L"1.50 h"}, {{0, 0}, L"Current limit"},
        {{5*hour, 6*hour}, L"Short-term (varying windows)"}, {{0, 5*hour}, L"Short-term (varying windows)"}};
    const std::array<std::wstring, 5> antigravityDurations{
        L"5h", L"1.50 h", L"current limit", L"short-term (varying windows)", L"short-term (varying windows)"};
    for (size_t d = 0; d < durations.size(); ++d) Run("duration labels/" + std::to_string(d), [&, d] {
        auto state = Model({1, 3});
        for (size_t account = 0; account < 2; ++account) for (size_t i = 0; i < 2; ++i) {
            auto& sample = AddRow(state, account, baseTime + 1000 + i*60000, {{0, 10}, {4, 20}});
            sample.meters[0].durationMs = sample.meters[4].durationMs = durations[d].first[i];
        }
        state.view = HistoryView::ShortTerm;
        auto built = Build(state);
        CHECK(Series(built, 0, 0).hoverTitle == state.accounts[0].label + L" · " + durations[d].second + L" used (%)");
        CHECK(Series(built, 1, 0).hoverTitle == state.accounts[1].label + L" · Gemini " + antigravityDurations[d] + L" used (%)");
        CHECK(Series(built, 1, 4).hoverTitle == state.accounts[1].label + L" · Claude/GPT " + antigravityDurations[d] + L" used (%)");
        CHECK(Series(built, 1, 4).legendTitle == state.accounts[1].label + L" · Claude/GPT " + antigravityDurations[d]);
        state.selected = 1;
        CHECK(Series(Build(state), 1, 4).legendTitle == L"Claude/GPT " + antigravityDurations[d]);
    });
    Run("remaining mode: titles name percentages left; legends, dollars, credits and statistics unchanged", [] {
        auto state = Model(); AllLanes(state);
        state.view = HistoryView::AllMeters;
        auto used = Build(state);
        CHECK(Series(used, 0, 3).hoverTitle == L"account-100 · Extra budget used (%)");
        state.remaining = true;
        auto left = Build(state);
        CHECK(left.series.size() == used.series.size());
        CHECK(Series(left, 0, 0).hoverTitle == L"account-100 · 5h left (%)");
        CHECK(Series(left, 0, 1).hoverTitle == L"account-100 · Weekly left (%)");
        CHECK(Series(left, 0, 3).hoverTitle == L"account-100 · Extra budget left (%)");
        CHECK(Series(left, 2, 1).hoverTitle == L"account-102 · Gemini weekly left (%)");
        CHECK(Series(left, 2, 4).hoverTitle == L"account-102 · Claude/GPT 5h left (%)");
        CHECK(Series(left, 0, 6).hoverTitle == L"account-100 · Extra spending (USD)");
        CHECK(Series(left, 1, 7).hoverTitle == L"account-101 · Credits remaining");
        CHECK(Series(left, 0, 1).legendTitle == L"account-100 · Weekly" && Series(left, 2, 4).legendTitle == L"account-102 · Claude/GPT 5h");
        // Statistics and ranges stay in recorded (used) values: weekly 20 then 25.
        const auto& weekly = Series(left, 0, 1);
        CHECK(weekly.current.peak == 25 && weekly.current.latest == 25 && weekly.low == 0 && weekly.high == 25);
    });
    Run("duration labels include previous period, ignore missing/failed/outside", [] {
        auto state = Model({1}); state.overlay = true; state.readFrom -= kHistoryWeekMs;
        AddRow(state, 0, state.readFrom - 1, {{0, 10}}).meters[0].durationMs = hour;
        AddRow(state, 0, state.readFrom + 1000, {{0, 10}}).meters[0].durationMs = 6*hour;
        AddRow(state, 0, state.from + 1000, {{0, 20}});
        AddRow(state, 0, state.from + 61000, {{0, -1}}).meters[0].durationMs = hour;
        auto& failedRow = AddRow(state, 0, state.from + 121000, {{0, 30}});
        failedRow.failed = 1; failedRow.meters[0].durationMs = hour;
        AddRow(state, 0, state.to, {{0, 10}}).meters[0].durationMs = hour;
        state.view = HistoryView::ShortTerm;
        CHECK(Series(Build(state), 0, 0).hoverTitle.find(L"varying windows") != std::wstring::npos);
        state.overlay = false; state.readFrom = state.from;
        CHECK(Series(Build(state), 0, 0).hoverTitle.find(L"5h used") != std::wstring::npos);
    });

    // Every break is tested in the same pixel and in separate pixels; use raw sourceRow
    // provenance to ensure the reducer never silently joins discontinuous observations.
    for (int kind = 0; kind < 9; ++kind) for (int width : {1, 1000}) {
        Run("non-connection kind=" + std::to_string(kind) + "/width=" + std::to_string(width), [=] {
            auto state = Model({1}); state.to = baseTime + 1000000;
            AddRow(state, 0, baseTime + 1000, {{1, 80}});
            AddRow(state, 0, baseTime + 61000, {{1, 100}});
            if (kind == 0) state.snapshot.samples[1].meters[1].durationMs += hour;
            if (kind == 1) { state.snapshot.samples[0].meters[1].resetMs = baseTime + 10000000; state.snapshot.samples[1].meters[1].resetMs = baseTime + 20000000; }
            if (kind == 2) state.snapshot.samples[1].timeMs = baseTime + 181001;
            if (kind == 3) state.snapshot.samples[1].session++;
            if (kind == 4) state.snapshot.samples[1].intervalMs = 0;
            if (kind == 5 || kind == 6 || kind == 7) {
                if (kind == 5) state.snapshot.samples[1].meters[1].value = -1;
                if (kind == 6) state.snapshot.samples[1].failed = 1;
                if (kind == 7) state.snapshot.samples[1].meters[1].value = std::numeric_limits<double>::quiet_NaN();
                AddRow(state, 0, baseTime + 121000, {{1, 95}});
            }
            if (kind == 8) state.snapshot.samples[0].meters[1].resetMs = state.snapshot.samples[1].meters[1].resetMs = baseTime + 31000;
            const auto& rows = state.accounts[0].rows;
            auto render = HistoryReduceRows(state.snapshot.samples, rows, 1, state.from, state.to, width, Never);
            CHECK(render.points.size() == 2);
            CHECK(!render.points[0].connect); CHECK(!render.points[1].connect);
            CHECK(render.points[0].sourceRow == 0);
            CHECK(render.points[1].sourceRow == (kind >= 5 && kind <= 7 ? 2 : 1));
            // Reset glyphs were removed; the actual reset still splits the data and integral.
            if (kind == 0 || kind == 1 || kind == 8)
                CHECK(HistoryConnection(state.snapshot.samples[0], state.snapshot.samples[1], 1) == HistoryBreak::Reset);
            else CHECK(HistoryConnection(state.snapshot.samples[0], state.snapshot.samples[1], 1) != HistoryBreak::None);
            auto stats = HistoryAnalyzeRows(state.snapshot.samples, rows, 1, state.from, state.to, state.redThreshold, Never);
            CHECK(stats.coverageMs == 0); CHECK(stats.nearLimitMs == 0); CHECK(stats.validSamples == 2);
        });
    }

    Run("reduction preserves first/min/max/last with raw statistics", [] {
        auto state = Model({1}); state.to = baseTime + 1000000;
        std::vector<double> values{20, 25, 5, 100, 40, 60};
        for (size_t i = 0; i < values.size(); ++i) AddRow(state, 0, baseTime + 1000 + i*60000, {{1, values[i]}});
        const auto& rows = state.accounts[0].rows;
        auto reduced = HistoryReduceRows(state.snapshot.samples, rows, 1, state.from, state.to, 1, Never);
        CHECK(reduced.points.size() == 4);
        const size_t wanted[] = {0, 2, 3, 5};
        for (size_t i = 0; i < 4; ++i) {
            CHECK(reduced.points[i].sourceRow == wanted[i]); CHECK(reduced.points[i].value == values[wanted[i]]);
            CHECK(reduced.points[i].connect == (i != 0));
        }
        auto stats = HistoryAnalyzeRows(state.snapshot.samples, rows, 1, state.from, state.to, state.redThreshold, Never);
        CHECK(stats.validSamples == 6); CHECK(stats.peak == 100); CHECK(stats.latest == 60);
        CHECK(stats.coverageMs == 300000);
        CHECK(std::abs(stats.nearLimitMs - (60000.0*10/95 + 60000.0*10/60)) < 1e-7);
    });
    Run("period boundaries do not extrapolate or bridge", [] {
        auto state = Model({1}); state.to = baseTime + 120000;
        AddRow(state, 0, baseTime - 1, {{1, 99}});
        AddRow(state, 0, baseTime, {{1, 10}});
        AddRow(state, 0, baseTime + 60000, {{1, 20}});
        AddRow(state, 0, state.to, {{1, 100}});
        const auto& samples = state.snapshot.samples;
        const auto& rows = state.accounts[0].rows;
        auto stats = HistoryAnalyzeRows(samples, rows, 1, state.from, state.to, state.redThreshold, Never);
        CHECK(stats.validSamples == 2); CHECK(stats.peak == 20); CHECK(stats.coverageMs == 60000);
        auto render = HistoryReduceRows(samples, rows, 1, state.from, state.to, 200, Never);
        CHECK(render.points.size() == 2); CHECK(render.points[0].sourceRow == 1);
        CHECK(!render.points[0].connect); CHECK(render.points[1].sourceRow == 2);
        CHECK(HistoryReduceRows(samples, rows, 1, state.from, state.to, 0, Never).points.empty());
        CHECK(HistoryReduceRows(samples, rows, 1, state.to, state.from, 100, Never).points.empty());
    });
    Run("reducer retains both extrema around multiple same-pixel breaks", [] {
        auto state = Model({1}); state.to = baseTime + 1000000;
        const double values[] = {5, 80, 20, -1, 30, 95, 40, 10};
        for (size_t i = 0; i < std::size(values); ++i) {
            auto& row = AddRow(state, 0, baseTime + 1000 + i*60000, {{1, values[i]}});
            if (i >= 7) ++row.session;
        }
        auto render = HistoryReduceRows(state.snapshot.samples, state.accounts[0].rows, 1, state.from, state.to, 1, Never);
        CHECK(render.points.size() == 7);
        const bool connects[] = {false, true, true, false, true, true, false};
        const size_t rows[] = {0, 1, 2, 4, 5, 6, 7};
        for (size_t i = 0; i < 7; ++i) { CHECK(render.points[i].connect == connects[i]); CHECK(render.points[i].sourceRow == rows[i]); }
    });

    Run("collected accounts: settings order, skipped IDs, removed accounts from samples in range, labelled by "
        "journal ID", [] {
        auto config = [](PCWSTR provider, PCWSTR label, uint64_t id) {
            AccountConfig account; account.provider = provider; account.label = label; account.historyId = id;
            return account;
        };
        std::vector<AccountConfig> configured{
            config(L"openai", L"Work", 0x30), config(L"anthropic", L"Home", 0x10),
            config(L"anthropic", L"No history", 0), config(L"antigravity", L"Duplicate", 0x10),
            config(L"antigravity", L"Lab", 0x20), config(L"openai", L"Idle", 0x40)};
        configured[1].recordHistory = false; configured[1].hidden = true;
        configured[1].showBars[kWeeklyBar] = false; configured[1].showBars[kFableWeeklyBar] = true;
        const ULONGLONG from = baseTime, to = baseTime + hour;
        std::vector<HistorySample> samples;
        for (auto [id, provider, time] : std::initializer_list<std::tuple<uint64_t, uint32_t, ULONGLONG>>{
                 {0x77, 1, from - kHistoryDayMs}, {0x10, 1, from - 1}, {0x10, 1, from}, {0xab, 2, from + 1},
                 {0, 1, from + 2}, {0x30, 2, from + 3}, {0xef, 9, from + 4}, {0x0123456789abcdefULL, 3, from + 5},
                 {0xab, 2, from + 6}, {0x20, 3, to - 1}, {0xcd, 1, to}}) {
            HistorySample sample; sample.accountId = id; sample.provider = provider; sample.timeMs = time;
            samples.push_back(sample);
        }
        struct Expected {
            uint64_t id; uint32_t provider; std::wstring label;
            bool configured, recording, hidden, weekly, fable; std::vector<size_t> rows;
        };
        const std::vector<Expected> wanted{
            {0x30, 2, L"Work", true, true, false, true, false, {5}},
            {0x10, 1, L"Home", true, false, true, false, true, {2}},
            {0x20, 3, L"Lab", true, true, false, true, false, {9}},
            {0x40, 2, L"Idle", true, true, false, true, false, {}},
            {0xab, 2, L"account_00000000000000ab", false, false, false, true, true, {3, 8}},
            {0xef, 9, L"account_00000000000000ef", false, false, false, true, true, {6}},
            {0x0123456789abcdefULL, 3, L"account_0123456789abcdef", false, false, false, true, true, {7}},
        };
        auto accounts = HistoryCollectAccounts(configured, samples, from, to, Never);
        CHECK(accounts.size() == wanted.size());
        for (size_t i = 0; i < std::min(accounts.size(), wanted.size()); ++i) {
            const auto& a = accounts[i]; const auto& w = wanted[i];
            CHECK(a.id == w.id); CHECK(a.provider == w.provider); CHECK(a.label == w.label);
            CHECK(a.configured == w.configured); CHECK(a.recording == w.recording); CHECK(a.hidden == w.hidden);
            CHECK(a.showWeekly == w.weekly); CHECK(a.showFableWeekly == w.fable); CHECK(a.rows == w.rows);
        }
        // A removed account with samples in range is listed, so a request selects it; one
        // outside the range isn't, so its request keeps the selection.
        CHECK(HistoryResolveSelection(accounts, 0x10, 0xab) == 4);
        CHECK(HistoryResolveSelection(accounts, 0x10, 0xcd) == 1);
        CHECK(HistoryCollectAccounts({}, samples, to, from, Never).empty());
    });

    Run("provider codes: one journal code per provider, OpenCode Go's 5; configured accounts collect with theirs", [] {
        CHECK(HistoryProviderCode(L"anthropic") == 1 && HistoryProviderCode(L"openai") == 2 &&
              HistoryProviderCode(L"antigravity") == 3 && HistoryProviderCode(L"copilot") == 4 &&
              HistoryProviderCode(L"opencode") == 5);
        CHECK(HistoryProviderCode(L"") == 0 && HistoryProviderCode(L"OpenCode") == 0);
        AccountConfig account; account.provider = L"opencode"; account.label = L"OC"; account.historyId = 0x50;
        auto accounts = HistoryCollectAccounts({account}, {}, baseTime, baseTime + hour, Never);
        CHECK(accounts.size() == 1 && accounts[0].provider == 5 && accounts[0].label == L"OC" && accounts[0].configured);
    });

    Run("selection resolution: kept, listed request wins, unlisted or zero request keeps, vanished selects All", [] {
        std::vector<HistoryUiAccount> accounts(3);
        accounts[0].id = 0x10; accounts[1].id = 0x20; accounts[2].id = 0xab;
        CHECK(HistoryResolveSelection(accounts, 0x20, 0) == 1);
        CHECK(HistoryResolveSelection(accounts, 0x20, 0xab) == 2);
        CHECK(HistoryResolveSelection(accounts, 0x20, 0x20) == 1);
        CHECK(HistoryResolveSelection(accounts, 0, 0x10) == 0);
        CHECK(HistoryResolveSelection(accounts, 0x20, 0x99) == 1);
        CHECK(HistoryResolveSelection(accounts, 0, 0x99) == -1);
        CHECK(HistoryResolveSelection(accounts, 0, 0) == -1);
        CHECK(HistoryResolveSelection(accounts, 0x99, 0) == -1);
        CHECK(HistoryResolveSelection(accounts, 0x99, 0x98) == -1);
        CHECK(HistoryResolveSelection(accounts, 0x99, 0x10) == 0);
        CHECK(HistoryResolveSelection({}, 0x10, 0x10) == -1);
    });

    // A predicate that turns true at its flip-th call and counts every call.
    Run("cancellation stops reduction and statistics at the next row", [] {
        auto state = Model({1}); state.to = baseTime + 1000000;
        for (size_t i = 0; i < 6; ++i) AddRow(state, 0, baseTime + 1000 + i*60000, {{1, 10.0 + double(i)}});
        const auto& samples = state.snapshot.samples;
        const auto& rows = state.accounts[0].rows;
        CHECK(HistoryReduceRows(samples, rows, 1, state.from, state.to, 1000, Never).points.size() == 6);
        CHECK(HistoryAnalyzeRows(samples, rows, 1, state.from, state.to, 90, Never).validSamples == 6);
        for (int flip = 1; flip <= 6; ++flip) {
            int calls = 0;
            auto cancelled = [&calls, flip] { return ++calls >= flip; };
            CHECK(HistoryReduceRows(samples, rows, 1, state.from, state.to, 1000, cancelled).points.empty());
            CHECK(calls == flip);
            calls = 0;
            auto stats = HistoryAnalyzeRows(samples, rows, 1, state.from, state.to, 90, cancelled);
            CHECK(calls == flip); CHECK(stats.validSamples == size_t(flip - 1)); CHECK(stats.hasValue == (flip > 1));
        }
    });

    Run("cancellation stops series building and account collection", [] {
        auto always = [] { return true; };
        auto state = Model({1, 2}); AllLanes(state); state.view = HistoryView::AllMeters;
        CHECK(Build(state).series.size() == 8);
        auto built = HistoryBuildSeries(state.snapshot.samples, state.accounts, state, always);
        CHECK(built.series.empty()); CHECK(built.noReadingAccounts.empty());
        // Cancelled while the first account is built: the second is never visited.
        int calls = 0;
        built = HistoryBuildSeries(state.snapshot.samples, state.accounts, state, [&calls] { return ++calls >= 2; });
        for (const auto& line : built.series) CHECK(line.account == 0);
        for (size_t account : built.noReadingAccounts) CHECK(account != 1);
        std::vector<AccountConfig> configured(2);
        configured[0].provider = L"anthropic"; configured[0].label = L"A"; configured[0].historyId = 0x10;
        configured[1].provider = L"openai"; configured[1].label = L"B"; configured[1].historyId = 0x20;
        std::vector<HistorySample> samples(3);
        for (size_t i = 0; i < samples.size(); ++i) { samples[i].provider = 1; samples[i].timeMs = baseTime + i; }
        samples[0].accountId = 0x10; samples[1].accountId = 0x30; samples[2].accountId = 0x20;
        CHECK(HistoryCollectAccounts(configured, samples, baseTime, baseTime + hour, Never).size() == 3);
        CHECK(HistoryCollectAccounts(configured, samples, baseTime, baseTime + hour, always).empty());
        // Cancelled after the configured entries: they stay listed, without rows or removed accounts.
        calls = 0;
        auto accounts = HistoryCollectAccounts(configured, samples, baseTime, baseTime + hour, [&calls] { return ++calls > 2; });
        CHECK(accounts.size() == 2);
        for (const auto& account : accounts) CHECK(account.rows.empty());
    });
