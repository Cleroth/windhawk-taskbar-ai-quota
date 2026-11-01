// Grouping, selection, reduction, statistics and in-memory CSV.
    Run("defaults and combo contents", [] {
        auto state = Fixture(); AllLanes(state);
        CHECK(state.selected == -1); CHECK(state.allAccountsView == HistoryView::Weekly);
        CHECK(state.accountView == HistoryView::AllMeters);
        CHECK(state.accounts[0].showWeekly); CHECK(state.accounts[0].showFableWeekly);
        HistoryRebuildView(state);
        CHECK(state.visible.size() == 6);
        CHECK((Meters(state, 0) == std::set<int>{1, 2}));
        CHECK((Meters(state, 1) == std::set<int>{1}));
        CHECK((Meters(state, 2) == std::set<int>{1, 2, 5}));
        CHECK((controls[state.meterCombo].items == std::vector<std::wstring>{L"Short-term", L"Weekly", L"Extra usage", L"All meters"}));
        CHECK(controls[state.meterCombo].selection == 1);
        CHECK(controls[state.exportButton].text == L"&Export all CSV...");
        SelectAccount(state, 0);
        CHECK(state.accountView == HistoryView::AllMeters);
        CHECK((Meters(state) == std::set<int>{0, 1, 2, 3, 6}));
        CHECK(controls[state.meterCombo].items.size() == 4);
        CHECK(controls[state.meterCombo].items[3] == L"All meters");
        CHECK(controls[state.meterCombo].selection == 3);
        CHECK(controls[state.exportButton].text == L"&Export CSV...");
        SelectAccount(state, -1); SelectView(state, HistoryView::AllMeters);
        CHECK(state.visible.size() == 13); CHECK(state.noReadings.empty());
        CHECK((Meters(state, 0) == std::set<int>{0, 1, 2, 3, 6}));
        CHECK((Meters(state, 1) == std::set<int>{0, 1, 7}));
        CHECK((Meters(state, 2) == std::set<int>{0, 1, 2, 4, 5}));
        CHECK(controls[state.meterCombo].selection == 3);
        CHECK(controls[state.exportButton].text == L"&Export all CSV...");
    });

    const std::array<std::array<std::set<int>, 4>, 3> expected{{
        {{{0}, {1, 2}, {3, 6}, {0, 1, 2, 3, 6}}},
        {{{0}, {1}, {7}, {0, 1, 7}}},
        {{{0, 4}, {1, 2, 5}, {}, {0, 1, 2, 4, 5}}},
    }};
    for (int selected : {-1, 0}) for (uint32_t provider = 1; provider <= 3; ++provider) {
        for (int view = 0; view < 4; ++view) {
            std::string key = "p" + std::to_string(provider) + "/v" + std::to_string(view) + "/mode=" + std::to_string(selected);
            Run("provider/view inclusion " + key, [&, provider, view, selected] {
                auto state = Fixture({provider}); AllLanes(state);
                state.selected = selected;
                state.allAccountsView = state.accountView = HistoryView(view);
                HistoryRebuildView(state);
                CHECK(Meters(state) == expected[provider - 1][view]);
                CHECK(state.noReadings.empty());
                for (const auto& line : state.visible) {
                    CHECK(line.current.hasValue); CHECK(line.current.validSamples == 2);
                    CHECK(line.plotted == (line.meter != 6 || view == 3));
                }
                if (provider == 1 && (view == 1 || view == 3)) CHECK(Series(state, 0, 2).title.find(L"Fable weekly") != std::wstring::npos);
                if (provider == 3 && (view == 1 || view == 3)) {
                    CHECK(Series(state, 0, 1).title.find(L"Gemini weekly") != std::wstring::npos); CHECK(Series(state, 0, 2).title.find(L"Argon weekly") != std::wstring::npos);
                    CHECK(Series(state, 0, 5).title.find(L"Claude/GPT weekly") != std::wstring::npos);
                }
            });
            Run("compact no-readings " + key, [&, provider, view, selected] {
                auto state = Fixture({provider});
                state.selected = selected;
                state.allAccountsView = state.accountView = HistoryView(view);
                HistoryRebuildView(state);
                CHECK(state.visible.empty());
                CHECK(state.noReadings.size() == (expected[provider - 1][view].empty() ? 0 : 1));
                if (!state.noReadings.empty()) {
                    CHECK(state.noReadings[0] == state.accounts[0].title);
                    CHECK(lastScroll.nMax == state.graphHeight + 2 * 24 - 1);
                }
            });
            Run("previous-only data retained " + key, [&, provider, view, selected] {
                auto state = Fixture({provider}); state.overlay = true; state.readFrom -= kHistoryWeekMs;
                AllLanes(state, true);
                state.selected = selected;
                state.allAccountsView = state.accountView = HistoryView(view);
                HistoryRebuildView(state);
                CHECK(Meters(state) == expected[provider - 1][view]);
                CHECK(state.noReadings.empty());
                for (const auto& line : state.visible) {
                    CHECK(!line.current.hasValue); CHECK(line.previous.hasValue);
                    CHECK(line.render[0].points.empty());
                    CHECK(line.render[1].points.size() == (line.plotted ? 2 : 0));
                }
                state.overlay = false; state.readFrom = state.from; HistoryRebuildView(state);
                CHECK(state.visible.empty());
            });
        }
    }
    for (int selected : {-1, 0}) for (uint32_t provider : {0U, 4U, UINT_MAX}) for (int view = 0; view < 4; ++view) {
        Run("unsupported provider " + std::to_string(provider) + "/v" + std::to_string(view) + "/mode=" + std::to_string(selected), [=] {
            auto state = Fixture({provider}); AllLanes(state);
            state.allAccountsView = state.accountView = HistoryView(view);
            state.selected = selected;
            HistoryRebuildView(state);
            CHECK(state.visible.empty()); CHECK(state.noReadings.empty());
        });
    }

    // Explicit flag cases: no second implementation of the production meter mask.
    const std::array<std::pair<bool, bool>, 4> weeklyFlags{{{false, false}, {true, false}, {false, true}, {true, true}}};
    const std::array<std::set<int>, 4> weeklyMeters{{{}, {1}, {2}, {1, 2}}};
    for (int selected : {-1, 0}) for (size_t flags = 0; flags < weeklyFlags.size(); ++flags) {
        for (int readings : {0, 1, 2}) Run("Anthropic weekly flags=" + std::to_string(flags) +
                "/readings=" + std::to_string(readings) + "/mode=" + std::to_string(selected), [&, selected, flags, readings] {
            auto state = Fixture({1}); state.selected = selected; state.enabled = true;
            state.accounts[0].recording = false;
            state.accounts[0].showWeekly = weeklyFlags[flags].first;
            state.accounts[0].showFableWeekly = weeklyFlags[flags].second;
            state.overlay = readings == 2;
            if (state.overlay) state.readFrom -= kHistoryWeekMs;
            if (readings) AllLanes(state, state.overlay);
            SelectView(state, HistoryView::Weekly);
            CHECK(Meters(state) == (readings ? weeklyMeters[flags] : std::set<int>{}));
            CHECK(state.noReadings.size() == (!readings && !weeklyMeters[flags].empty() ? 1 : 0));
            CHECK(StatusCount(state, 0, L"[recording off]") == (weeklyMeters[flags].empty() ? 0 : 1));
            for (const auto& line : state.visible) {
                CHECK(line.current.hasValue == (readings == 1)); CHECK(line.previous.hasValue == (readings == 2));
                CHECK(line.render[0].points.size() == (readings == 1 ? 2 : 0));
                CHECK(line.render[1].points.size() == (readings == 2 ? 2 : 0));
            }
            // Neither Short-term, Extra usage nor All meters honors the weekly display flags.
            for (int view : {0, 2, 3}) {
                SelectView(state, HistoryView(view));
                CHECK(Meters(state) == (readings ? expected[0][view] : std::set<int>{}));
                CHECK(state.noReadings.size() == (readings ? 0 : 1));
                CHECK(StatusCount(state, 0, L"[recording off]") == 1);
                if (readings && view == 3) {
                    CHECK(Series(state, 0, 6).plotted);
                    CHECK(Series(state, 0, 6).render[state.overlay ? 1 : 0].points.size() == 2);
                }
            }
            if (state.overlay) {
                state.overlay = false; state.readFrom = state.from;
                SelectView(state, HistoryView::Weekly);
                CHECK(state.visible.empty());
                CHECK(state.noReadings.size() == (weeklyMeters[flags].empty() ? 0 : 1));
            }
        });
    }

    for (int selected : {-1, 0}) for (bool previous : {false, true}) {
        Run("disabled weekly readings do not fill enabled meter/mode=" + std::to_string(selected) +
                "/previous=" + std::to_string(previous), [=] {
            auto state = Fixture({1}); state.selected = selected; state.overlay = previous;
            if (previous) state.readFrom -= kHistoryWeekMs;
            state.accounts[0].showFableWeekly = false;
            AddRow(state, 0, state.readFrom + 1000, {{2, 30}});
            SelectView(state, HistoryView::Weekly);
            CHECK(state.visible.empty()); CHECK(state.noReadings == std::vector<std::wstring>{state.accounts[0].title});
            state.accounts[0].showWeekly = false; state.accounts[0].showFableWeekly = true;
            HistoryRebuildView(state);
            CHECK(Meters(state) == std::set<int>{2}); CHECK(state.noReadings.empty());
            CHECK(Series(state, 0, 2).current.hasValue == !previous);
            CHECK(Series(state, 0, 2).previous.hasValue == previous);
        });
    }

    Run("mixed weekly flags stay account-local; removed and other providers ignore them", [&] {
        auto state = Fixture({1, 1, 1, 1, 1, 2, 3}); AllLanes(state);
        for (auto& account : state.accounts) account.showWeekly = account.showFableWeekly = false;
        for (size_t i = 0; i < weeklyFlags.size(); ++i) {
            state.accounts[i].showWeekly = weeklyFlags[i].first;
            state.accounts[i].showFableWeekly = weeklyFlags[i].second;
        }
        state.accounts[4].configured = false; state.accounts[6].showWeekly = true; state.accounts[6].showArgonWeekly = true;
        const std::array<std::set<int>, 7> mixedMeters{{{}, {1}, {2}, {1, 2}, {1, 2}, {1}, {1, 2, 5}}};
        SelectView(state, HistoryView::Weekly);
        CHECK(state.visible.size() == 10); CHECK(state.noReadings.empty());
        for (size_t i = 0; i < state.accounts.size(); ++i) CHECK(Meters(state, i) == mixedMeters[i]);
        for (size_t i = 0; i < state.accounts.size(); ++i) {
            SelectAccount(state, int(i)); SelectView(state, HistoryView::Weekly);
            CHECK(Meters(state, i) == mixedMeters[i]); CHECK(state.noReadings.empty());
            CHECK(state.visible.size() == mixedMeters[i].size());
        }
        // A disabled account must not leave a compact no-readings row behind either.
        for (auto& account : state.accounts) account.rows.clear();
        SelectAccount(state, -1);
        CHECK(state.visible.empty()); CHECK(state.noReadings.size() == 6);
        for (size_t i = 1; i < state.accounts.size(); ++i) CHECK(state.noReadings[i - 1] == state.accounts[i].title);
        for (size_t i = 0; i < state.accounts.size(); ++i) {
            SelectAccount(state, int(i));
            CHECK(state.visible.empty()); CHECK(state.noReadings.size() == (i == 0 ? 0 : 1));
            if (i) CHECK(state.noReadings[0] == state.accounts[i].title);
        }
    });

    Run("Extra USD is detail-only, AllMeters restores USD plot/cache", [] {
        auto state = Fixture(); state.overlay = true; state.readFrom -= kHistoryWeekMs;
        AllLanes(state, true); AllLanes(state);
        SelectView(state, HistoryView::ExtraUsage);
        CHECK(state.visible.size() == 3); CHECK(state.noReadings.empty());
        CHECK(state.axes[0].visible); CHECK(!state.axes[1].visible); CHECK(state.axes[2].visible);
        CHECK(state.axes[1].low == 0); CHECK(state.axes[1].high == 1);
        const auto& usd = Series(state, 0, 6);
        CHECK(!usd.plotted); CHECK(usd.current.latest == 15.5); CHECK(usd.current.peak == 15.5);
        CHECK(usd.previous.hasValue); CHECK(usd.render[0].points.empty()); CHECK(usd.render[1].points.empty());
        CHECK(usd.title.find(L"details only") != std::wstring::npos);
        SelectAccount(state, 0);
        CHECK(state.axes[1].visible); CHECK(Series(state, 0, 6).plotted);
        CHECK(Series(state, 0, 6).render[0].points.size() == 2);
        CHECK(Series(state, 0, 6).render[1].points.size() == 2);
        SelectView(state, HistoryView::ExtraUsage);
        CHECK(!state.axes[1].visible); CHECK(!Series(state, 0, 6).plotted);
        CHECK(Series(state, 0, 6).render[0].points.empty());
    });

    for (bool previous : {false, true}) Run(std::string("USD-only account not empty/") + (previous ? "previous" : "current"), [=] {
        auto state = Fixture({1}); state.overlay = previous;
        if (previous) state.readFrom -= kHistoryWeekMs;
        AddRow(state, 0, state.readFrom + 1000, {{6, 30}});
        AddRow(state, 0, state.readFrom + 61000, {{6, 5}});
        SelectView(state, HistoryView::ExtraUsage);
        CHECK(state.visible.size() == 1); CHECK(state.noReadings.empty());
        const auto& usd = state.visible[0]; const auto& stats = previous ? usd.previous : usd.current;
        CHECK(!usd.plotted); CHECK(stats.latest == 5); CHECK(stats.peak == 30);
        CHECK(state.axes[0].visible); CHECK(!state.axes[1].visible); CHECK(!state.axes[2].visible);
    });

    Run("status emitted once/account, including compact and USD-only", [] {
        auto state = Fixture({1, 2, 1, 1, 3}); state.enabled = true;
        AllLanes(state);
        state.accounts[0].recording = false;
        state.accounts[1].hidden = true;
        state.accounts[2].recording = false; state.accounts[2].rows.clear();
        state.accounts[3].recording = false;
        for (auto row : state.accounts[3].rows) for (int meter = 0; meter < 6; ++meter)
            state.snapshot.samples[row].meters[meter].value = -1;
        state.accounts[4].recording = false;
        for (auto view : {HistoryView::ShortTerm, HistoryView::Weekly, HistoryView::ExtraUsage, HistoryView::AllMeters}) {
            SelectView(state, view);
            CHECK(StatusCount(state, 0, L"[recording off]") == 1);
            CHECK(StatusCount(state, 1, L"[paused]") == 1);
            CHECK(StatusCount(state, 2, L"[recording off]") == 1);
            CHECK(StatusCount(state, 3, L"[recording off]") == 1);
            CHECK(StatusCount(state, 4, L"[recording off]") == (view == HistoryView::ExtraUsage ? 0 : 1));
        }
        state.skipHidden = false; HistoryRebuildView(state);
        CHECK(StatusCount(state, 1, L"[paused]") == 0);
        state.enabled = false; HistoryRebuildView(state);
        CHECK(StatusCount(state, 0, L"[recording off]") == 0);
    });

    Run("view selections restore across account/all mode transitions", [] {
        auto state = Fixture(); AllLanes(state); HistoryRebuildView(state);
        SelectView(state, HistoryView::ExtraUsage);
        SelectAccount(state, 1); CHECK(state.accountView == HistoryView::AllMeters);
        SelectView(state, HistoryView::ShortTerm);
        SelectAccount(state, 2); CHECK(state.accountView == HistoryView::ShortTerm);
        CHECK(controls[state.meterCombo].selection == 0);
        SelectAccount(state, -1); CHECK(state.allAccountsView == HistoryView::ExtraUsage);
        CHECK(controls[state.meterCombo].selection == 2); CHECK(controls[state.meterCombo].items.size() == 4);
        SelectView(state, HistoryView::Weekly);
        SelectAccount(state, 0); CHECK(state.accountView == HistoryView::ShortTerm);
        SelectAccount(state, -1); CHECK(controls[state.meterCombo].selection == 1);
        SelectView(state, HistoryView::AllMeters);
        SelectAccount(state, 1); CHECK(state.accountView == HistoryView::ShortTerm);
        CHECK(controls[state.meterCombo].selection == 0);
        SelectAccount(state, -1); CHECK(state.allAccountsView == HistoryView::AllMeters);
        CHECK(controls[state.meterCombo].selection == 3); CHECK(state.visible.size() == 13);
        // Both modes clamp to the same four choices without changing the other memory.
        for (int selected : {-1, 0}) {
            SelectAccount(state, selected);
            auto otherView = selected < 0 ? state.accountView : state.allAccountsView;
            controls[state.meterCombo].selection = -2; DispatchSelection(&state, kHistoryMeterCombo);
            CHECK((selected < 0 ? state.allAccountsView : state.accountView) == HistoryView::ShortTerm);
            CHECK(controls[state.meterCombo].selection == 0);
            controls[state.meterCombo].selection = 99; DispatchSelection(&state, kHistoryMeterCombo);
            CHECK((selected < 0 ? state.allAccountsView : state.accountView) == HistoryView::AllMeters);
            CHECK(controls[state.meterCombo].selection == 3);
            CHECK((selected < 0 ? state.accountView : state.allAccountsView) == otherView);
        }
    });

    for (int selected : {-1, 0}) Run("CB_ERR preserves both view selections/mode=" + std::to_string(selected), [=] {
        for (auto allView : {HistoryView::ShortTerm, HistoryView::Weekly, HistoryView::ExtraUsage, HistoryView::AllMeters}) {
            for (auto accountView : {HistoryView::ShortTerm, HistoryView::Weekly, HistoryView::ExtraUsage, HistoryView::AllMeters}) {
                auto state = Fixture(); AllLanes(state);
                state.selected = selected; state.allAccountsView = allView; state.accountView = accountView;
                HistoryRebuildView(state);
                const auto items = controls[state.meterCombo].items;
                int resets = controls[state.meterCombo].resets;
                const auto* visible = state.visible.data();
                int renderWidth = state.renderWidth;
                state.scroll = 17;
                controls[state.meterCombo].selection = CB_ERR;
                DispatchSelection(&state, kHistoryMeterCombo);
                CHECK(state.selected == selected);
                CHECK(state.allAccountsView == allView); CHECK(state.accountView == accountView);
                CHECK(controls[state.meterCombo].resets == resets);
                CHECK(controls[state.meterCombo].items == items);
                CHECK(controls[state.meterCombo].selection == CB_ERR);
                CHECK(state.visible.data() == visible); CHECK(state.renderWidth == renderWidth);
                CHECK(state.scroll == 17);
                // Subsequent mode switches must still restore the remembered valid choice.
                SelectAccount(state, selected < 0 ? 0 : -1);
                CHECK(controls[state.meterCombo].selection == int(selected < 0 ? accountView : allView));
                SelectAccount(state, selected);
                CHECK(controls[state.meterCombo].selection == int(selected < 0 ? allView : accountView));
            }
        }
    });

    Run("all-account colors/widths stable when views omit preceding accounts", [] {
        auto state = Fixture({3, 1, 2, 1, 3, 2, 1, 2, 3, 1}); AllLanes(state);
        const COLORREF palette[] = {RGB(0,80,230), RGB(220,100,0), RGB(0,140,40), RGB(200,0,160),
            RGB(115,45,200), RGB(0,140,170), RGB(35,35,35), RGB(220,25,35)};
        for (auto view : {HistoryView::Weekly, HistoryView::ExtraUsage, HistoryView::ShortTerm, HistoryView::AllMeters, HistoryView::Weekly}) {
            SelectView(state, view);
            for (const auto& series : state.visible) {
                bool scoped = series.meter == 2 || series.meter == 4 || series.meter == 5;
                COLORREF color = palette[series.account % 8];
                if (scoped) color = RGB((GetRValue(color)*3+255)/4, (GetGValue(color)*3+255)/4, (GetBValue(color)*3+255)/4);
                CHECK(series.color == color); CHECK(series.stroke == (scoped ? 2 : 3));
            }
        }
        auto weekly = state.visible;
        SelectAccount(state, 4); SelectAccount(state, -1);
        CHECK(state.visible.size() == weekly.size());
        for (size_t i = 0; i < weekly.size(); ++i) {
            CHECK(state.visible[i].color == weekly[i].color); CHECK(state.visible[i].stroke == weekly[i].stroke);
        }
    });

    for (int account = 0; account < 3; ++account) Run("single-account meter styles stable/" + std::to_string(account), [=] {
        auto state = Fixture(); AllLanes(state); SelectAccount(state, account);
        std::map<int, std::pair<COLORREF, int>> styles;
        for (const auto& line : state.visible) styles[line.meter] = {line.color, line.stroke};
        for (auto view : {HistoryView::Weekly, HistoryView::ExtraUsage, HistoryView::ShortTerm, HistoryView::AllMeters}) {
            SelectView(state, view);
            for (const auto& line : state.visible) CHECK((styles.at(line.meter) == std::pair{line.color, line.stroke}));
        }
        SelectAccount(state, -1); SelectAccount(state, account);
        for (const auto& line : state.visible) CHECK((styles.at(line.meter) == std::pair{line.color, line.stroke}));
    });

    const std::vector<std::pair<std::vector<ULONGLONG>, std::wstring>> durations{
        {{5*hour, 5*hour}, L"5h"}, {{90*60000, 90*60000}, L"1.50 h"}, {{0, 0}, L"Current limit"},
        {{5*hour, 6*hour}, L"Short-term (varying windows)"}, {{0, 5*hour}, L"Short-term (varying windows)"}};
    const std::array<std::wstring, 5> antigravityDurations{
        L"5h", L"1.50 h", L"current limit", L"short-term (varying windows)", L"short-term (varying windows)"};
    for (size_t d = 0; d < durations.size(); ++d) Run("duration labels/" + std::to_string(d), [&, d] {
        auto state = Fixture({1, 3});
        for (size_t account = 0; account < 2; ++account) for (size_t i = 0; i < 2; ++i) {
            auto& sample = AddRow(state, account, baseTime + 1000 + i*60000, {{0, 10}, {4, 20}});
            sample.meters[0].durationMs = sample.meters[4].durationMs = durations[d].first[i];
        }
        SelectView(state, HistoryView::ShortTerm);
        CHECK(Series(state, 0, 0).title == state.accounts[0].title + L" · " + durations[d].second + L" used (%)");
        CHECK(Series(state, 1, 0).title == state.accounts[1].title + L" · Gemini " + antigravityDurations[d] + L" used (%)");
        CHECK(Series(state, 1, 4).title == state.accounts[1].title + L" · Claude/GPT " + antigravityDurations[d] + L" used (%)");
    });
    Run("duration labels include previous period, ignore missing/failed/outside", [] {
        auto state = Fixture({1}); state.overlay = true; state.readFrom -= kHistoryWeekMs;
        AddRow(state, 0, state.readFrom - 1, {{0, 10}}).meters[0].durationMs = hour;
        AddRow(state, 0, state.readFrom + 1000, {{0, 10}}).meters[0].durationMs = 6*hour;
        AddRow(state, 0, state.from + 1000, {{0, 20}});
        AddRow(state, 0, state.from + 61000, {{0, -1}}).meters[0].durationMs = hour;
        auto& failedRow = AddRow(state, 0, state.from + 121000, {{0, 30}});
        failedRow.failed = 1; failedRow.meters[0].durationMs = hour;
        AddRow(state, 0, state.to, {{0, 10}}).meters[0].durationMs = hour;
        SelectView(state, HistoryView::ShortTerm);
        CHECK(Series(state, 0, 0).title.find(L"varying windows") != std::wstring::npos);
        state.overlay = false; state.readFrom = state.from; HistoryRebuildView(state);
        CHECK(Series(state, 0, 0).title.find(L"5h used") != std::wstring::npos);
    });

    // Every break is tested in the same pixel and in separate pixels; use raw sourceRow
    // provenance to ensure the reducer never silently joins discontinuous observations.
    for (int kind = 0; kind < 9; ++kind) for (int width : {1, 1000}) {
        Run("non-connection kind=" + std::to_string(kind) + "/width=" + std::to_string(width), [=] {
            auto state = Fixture({1}); state.to = baseTime + 1000000;
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
            auto render = HistoryReducePeriod(state.snapshot, rows, 1, state.from, state.to, width);
            CHECK(render.points.size() == 2);
            CHECK(!render.points[0].connect); CHECK(!render.points[1].connect);
            CHECK(render.points[0].sourceRow == 0);
            CHECK(render.points[1].sourceRow == (kind >= 5 && kind <= 7 ? 2 : 1));
            // Reset glyphs were removed; the actual reset still splits the data and integral.
            if (kind == 0 || kind == 1 || kind == 8)
                CHECK(HistoryConnection(state.snapshot.samples[0], state.snapshot.samples[1], 1) == HistoryBreak::Reset);
            else CHECK(HistoryConnection(state.snapshot.samples[0], state.snapshot.samples[1], 1) != HistoryBreak::None);
            auto stats = HistoryAnalyze(state, state.accounts[0], 1, state.from, state.to);
            CHECK(stats.coverageMs == 0); CHECK(stats.nearLimitMs == 0); CHECK(stats.validSamples == 2);
        });
    }

    Run("reduction preserves first/min/max/last with raw statistics", [] {
        auto state = Fixture({1}); state.to = baseTime + 1000000;
        std::vector<double> values{20, 25, 5, 100, 40, 60};
        for (size_t i = 0; i < values.size(); ++i) AddRow(state, 0, baseTime + 1000 + i*60000, {{1, values[i]}});
        auto reduced = HistoryReducePeriod(state.snapshot, state.accounts[0].rows, 1, state.from, state.to, 1);
        CHECK(reduced.points.size() == 4);
        const size_t wanted[] = {0, 2, 3, 5};
        for (size_t i = 0; i < 4; ++i) {
            CHECK(reduced.points[i].sourceRow == wanted[i]); CHECK(reduced.points[i].value == values[wanted[i]]);
            CHECK(reduced.points[i].connect == (i != 0));
        }
        auto stats = HistoryAnalyze(state, state.accounts[0], 1, state.from, state.to);
        CHECK(stats.validSamples == 6); CHECK(stats.peak == 100); CHECK(stats.latest == 60);
        CHECK(stats.coverageMs == 300000);
        CHECK(std::abs(stats.nearLimitMs - (60000.0*10/95 + 60000.0*10/60)) < 1e-7);
    });
    Run("period boundaries do not extrapolate or bridge", [] {
        auto state = Fixture({1}); state.to = baseTime + 120000;
        AddRow(state, 0, baseTime - 1, {{1, 99}});
        AddRow(state, 0, baseTime, {{1, 10}});
        AddRow(state, 0, baseTime + 60000, {{1, 20}});
        AddRow(state, 0, state.to, {{1, 100}});
        auto stats = HistoryAnalyze(state, state.accounts[0], 1, state.from, state.to);
        CHECK(stats.validSamples == 2); CHECK(stats.peak == 20); CHECK(stats.coverageMs == 60000);
        auto render = HistoryReducePeriod(state.snapshot, state.accounts[0].rows, 1, state.from, state.to, 200);
        CHECK(render.points.size() == 2); CHECK(render.points[0].sourceRow == 1);
        CHECK(!render.points[0].connect); CHECK(render.points[1].sourceRow == 2);
        CHECK(HistoryReducePeriod(state.snapshot, state.accounts[0].rows, 1, state.from, state.to, 0).points.empty());
        CHECK(HistoryReducePeriod(state.snapshot, state.accounts[0].rows, 1, state.to, state.from, 100).points.empty());
    });
    Run("reducer retains both extrema around multiple same-pixel breaks", [] {
        auto state = Fixture({1}); state.to = baseTime + 1000000;
        const double values[] = {5, 80, 20, -1, 30, 95, 40, 10};
        for (size_t i = 0; i < std::size(values); ++i) {
            auto& row = AddRow(state, 0, baseTime + 1000 + i*60000, {{1, values[i]}});
            if (i >= 7) ++row.session;
        }
        auto render = HistoryReducePeriod(state.snapshot, state.accounts[0].rows, 1, state.from, state.to, 1);
        CHECK(render.points.size() == 7);
        const bool connects[] = {false, true, true, false, true, true, false};
        const size_t rows[] = {0, 1, 2, 4, 5, 6, 7};
        for (size_t i = 0; i < 7; ++i) { CHECK(render.points[i].connect == connects[i]); CHECK(render.points[i].sourceRow == rows[i]); }
    });
    Run("raw snapshot and CSV independent of grouped view/weekly flags/reduction", [&] {
        auto state = Fixture(); AllLanes(state);
        AddRow(state, 0, baseTime + 121000, {}).failed = 1;
        auto original = state.snapshot.samples;
        CapturedCsv baseline; size_t count = 0;
        CHECK(baseline.WriteSnapshot(state, &count)); CHECK(count == 31);
        CHECK(baseline.bytes.find("anthropic_extra_used,USD,12.5") != std::string::npos);
        CHECK(baseline.bytes.find("openai_credit_balance,credits,800") != std::string::npos);
        CHECK(baseline.bytes.find("account-100") == std::string::npos); CHECK(baseline.bytes.find("argon_weekly,percent,30") != std::string::npos); CHECK(baseline.bytes.find("fable_weekly,percent,30") != std::string::npos);
        CHECK(baseline.bytes.find(",1,1\r\n") != std::string::npos);
        SelectAccount(state, 0);
        CapturedCsv single; size_t singleCount = 0;
        CHECK(single.WriteSnapshot(state, &singleCount)); CHECK(singleCount == 15);
        for (int selected : {-1, 0}) for (auto [weekly, fable] : weeklyFlags) {
            state.accounts[0].showWeekly = weekly; state.accounts[0].showFableWeekly = fable;
            SelectAccount(state, selected);
            for (auto view : {HistoryView::ShortTerm, HistoryView::Weekly, HistoryView::ExtraUsage, HistoryView::AllMeters}) {
                SelectView(state, view);
                clientRect.right = 137; state.renderWidth = -1; HistoryRebuildRenderCache(state);
                CapturedCsv next; size_t nextCount = 0;
                CHECK(next.WriteSnapshot(state, &nextCount));
                CHECK(nextCount == (selected < 0 ? count : singleCount));
                CHECK(next.bytes == (selected < 0 ? baseline.bytes : single.bytes));
                CHECK(state.snapshot.samples.size() == original.size());
                CHECK(std::memcmp(original.data(), state.snapshot.samples.data(), original.size()*sizeof(HistorySample)) == 0);
            }
        }
    });
