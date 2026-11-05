// Edit controller, number parsing, projection and the Behavior/History pages: the real
// settings/edits.h against an in-memory store with deterministic failure injection.

// In-memory store: settings, statuses, stored sign-ins, an operation log, failures and unload on
// demand. Account operations resolve the account afresh (by history ID, else identity), as the
// production store does; a rename moves the account's stored sign-in, a removal deletes it and,
// when asked, the account's history.
struct TestStore final : SettingsStore {
    // Kept for 90 days, so the custom retention box edits it (kept forever: its own cases)
    Settings settings = [] {
        Settings s;
        s.historyRetentionDays = 90;
        return s;
    }();
    SettingsStatus status;
    std::vector<std::wstring> log;  // "load", "apply <field>=<value>",
                                    // "account <field> <label>=<value or text>", "add <provider> <label>"
    std::vector<uint64_t> signIns;  // Identities with a stored sign-in
    std::vector<uint64_t> historyDeleted;  // History IDs whose removal deleted their history
    bool historyKept = false;              // A removal asked to delete history keeps it, saying so
    int failNext = 0;               // The next N applies, account operations and adds fail
    int unloadAfter = -1;           // Unloading once this many of them ran
    int applies = 0;
    int presenceChecks = 0;
    uint64_t nextHistoryId = 0x100;
    bool generateHistoryIds = false;  // An account edit gives accounts without one a history ID
    std::function<void(Settings&)> normalize;  // What saving does after the assignment, if set

    SettingsBaseline Load() override {
        log.push_back(L"load");
        return {settings, status};
    }
    SettingsSaveResult Apply(SettingsField field, int value) override {
        log.push_back(L"apply " + std::to_wstring(int(field)) + L"=" + std::to_wstring(value));
        applies++;
        if (failNext > 0) {
            failNext--;
            return SettingsSaveResult::Failed;
        }
        Settings next = settings;
        SettingsAssign(next, field, value);
        if (normalize) normalize(next);
        bool changed = !(next == settings);
        settings = std::move(next);
        return changed ? SettingsSaveResult::Changed : SettingsSaveResult::Unchanged;
    }
    SettingsAccountResult Account(SettingsAccountRef ref, SettingsField field, int value,
                                  const std::wstring& text) override {
        std::vector<AccountConfig>& accounts = settings.accounts;
        auto at = std::find_if(accounts.begin(), accounts.end(), [&](const AccountConfig& account) {
            return ref.historyId ? account.historyId == ref.historyId : AccountIdentityHash(account) == ref.identity;
        });
        log.push_back(L"account " + std::to_wstring(int(field)) + L" " + (at == accounts.end() ? L"?" : at->label) +
                      L"=" + (text.empty() ? std::to_wstring(value) : text));
        applies++;
        SettingsAccountResult result;
        if (at == accounts.end()) {
            result.missing = true;
            return result;
        }
        if (failNext > 0) {
            failNext--;
            result.save = SettingsSaveResult::Failed;
            result.notice = L"Injected failure.";
            return result;
        }
        uint64_t identity = AccountIdentityHash(*at);
        size_t index = size_t(at - accounts.begin());
        result.account = {at->historyId, identity};
        switch (field) {
            case SettingsField::AccountSignIn:
            case SettingsField::AccountDashboard:
                return result;
            case SettingsField::AccountSignOut:
                std::erase(signIns, identity);
                return result;
            case SettingsField::AccountRemove:
                std::erase(signIns, identity);
                if (value && historyKept) {
                    result.notice = L"The account was removed, but its usage history could not be deleted and was kept.";
                    result.kind = SettingsNoticeKind::Warning;
                } else if (value) {
                    historyDeleted.push_back(at->historyId);
                }
                accounts.erase(at);
                result.save = SettingsSaveResult::Changed;
                return result;
            case SettingsField::AccountMove: {
                size_t to = size_t(std::clamp(value, 0, int(accounts.size()) - 1));
                AccountConfig moved = accounts[index];
                accounts.erase(accounts.begin() + ptrdiff_t(index));
                accounts.insert(accounts.begin() + ptrdiff_t(to), moved);
                result.save = to == index ? SettingsSaveResult::Unchanged : SettingsSaveResult::Changed;
                return result;
            }
            case SettingsField::AccountHidden: {
                long visible = std::count_if(accounts.begin(), accounts.end(),
                                             [](const AccountConfig& account) { return !account.hidden; });
                if (value && !at->hidden && visible <= 1) return result;
                result.save = at->hidden != (value != 0) ? SettingsSaveResult::Changed : SettingsSaveResult::Unchanged;
                at->hidden = value != 0;
                return result;
            }
            default: {
                AccountConfig next = *at;
                SettingsAccountAssign(next, field, value, text);
                bool duplicate = std::any_of(accounts.begin(), accounts.end(), [&](const AccountConfig& other) {
                    return &other != &*at && other.provider == next.provider && other.label == next.label;
                });
                if (duplicate) {
                    result.save = SettingsSaveResult::Failed;
                    result.notice = L"That provider and label are already configured.";
                    return result;
                }
                uint64_t renamed = AccountIdentityHash(next);
                if (renamed != identity && std::erase(signIns, identity)) signIns.push_back(renamed);
                result.save = next == *at ? SettingsSaveResult::Unchanged : SettingsSaveResult::Changed;
                *at = std::move(next);
                for (AccountConfig& account : accounts) {
                    if (generateHistoryIds && !account.historyId) account.historyId = nextHistoryId++;
                }
                result.account = {at->historyId, renamed};
                return result;
            }
        }
    }
    SettingsAccountResult AddAccount(const std::wstring& provider, const std::vector<std::wstring>& labels) override {
        applies++;
        SettingsAccountResult result;
        if (failNext > 0) {
            failNext--;
            log.push_back(L"add " + provider + L" failed");
            result.save = SettingsSaveResult::Failed;
            result.notice = L"Injected failure.";
            return result;
        }
        for (const std::wstring& label : labels) {
            if (std::any_of(settings.accounts.begin(), settings.accounts.end(), [&](const AccountConfig& account) {
                    return account.provider == provider && account.label == label;
                })) {
                continue;
            }
            AccountConfig account;
            account.provider = provider;
            account.label = label;
            account.historyId = nextHistoryId++;
            settings.accounts.push_back(account);
            log.push_back(L"add " + provider + L" " + label);
            result.save = SettingsSaveResult::Changed;
            result.account = {account.historyId, AccountIdentityHash(account)};
            return result;
        }
        log.push_back(L"add " + provider + L" none");
        result.save = SettingsSaveResult::Failed;
        result.notice = L"That provider and label are already configured.";
        return result;
    }
    bool HasStoredSignIn(uint64_t identity) override {
        presenceChecks++;
        return std::find(signIns.begin(), signIns.end(), identity) != signIns.end();
    }
    bool Unloading() override { return unloadAfter >= 0 && applies >= unloadAfter; }

    std::vector<std::wstring> Applied() const {
        std::vector<std::wstring> applied;
        for (const std::wstring& entry : log) {
            if (entry.starts_with(L"apply ")) applied.push_back(entry.substr(6));
        }
        return applied;
    }
};

static std::wstring Op(SettingsField field, int value) {
    return std::to_wstring(int(field)) + L"=" + std::to_wstring(value);
}

static constexpr SettingsField kRetention = SettingsField::RetentionDays;
static constexpr SettingsField kKeep = SettingsField::KeepHistory;
static constexpr SettingsField kPoll = SettingsField::PollMinutes;

static void RunEditTests() {
    Run("parse: spaces, sign, digit grouping; empty, malformed and overflow fail; range clamps", [] {
        SettingsNumberRange days{1, 3650};
        auto parse = [&](std::wstring_view text, wchar_t separator = L',') {
            SettingsParsedNumber parsed = SettingsParseNumber(text, days, separator);
            return parsed.ok ? parsed.value : -999;
        };
        CHECK(parse(L"") == -999);
        CHECK(parse(L"   ") == -999);
        CHECK(parse(L"-") == -999);
        CHECK(parse(L"12a") == -999);
        CHECK(parse(L"1 2") == -999);
        CHECK(parse(L" 7 ") == 7);
        CHECK(parse(L"\u00A07\t") == 7);
        CHECK(parse(L"+12") == 12);
        CHECK(parse(L"2,500") == 2500);
        CHECK(parse(L"1,234,567") == 3650);  // Valid grouping, above the range: clamped
        CHECK(parse(L"25,00") == -999);
        CHECK(parse(L",500") == -999);
        CHECK(parse(L"2,500,") == -999);
        CHECK(parse(L"1,,500") == -999);
        CHECK(parse(L"1234,567") == -999);
        CHECK(parse(L"2.500") == -999);       // Not this locale's separator
        CHECK(parse(L"2.500", L'.') == 2500);
        CHECK(parse(L"2\u00A0500", L'\u202F') == 2500);  // A space separator takes any space
        CHECK(parse(L"\uFF11\uFF12") == -999);  // Fullwidth digits
        CHECK(parse(L"99999999999999999999") == -999);
        CHECK(parse(L"2147483648") == -999);
        CHECK(parse(L"2147483647") == 3650);
        CHECK(parse(L"0") == 1);
        CHECK(parse(L"-5") == 1);
        CHECK(parse(L"5000") == 3650);
        CHECK(SettingsFormatNumber(1440, L',') == L"1,440");
        CHECK(SettingsFormatNumber(90, L',') == L"90");
        CHECK(SettingsFormatNumber(1234567, L'.') == L"1.234.567");
        CHECK(SettingsFormatNumber(0, L',') == L"0");
    });

    Run("assign: desired values, groups back to defaults, numbers clamped", [] {
        Settings s;
        SettingsAssign(s, SettingsField::ShowOn, 3);
        CHECK(s.taskbarMonitorMode == TaskbarMonitorMode::Specific && s.taskbarMonitorNumber == 3);
        SettingsAssign(s, SettingsField::ShowOn, -1);
        CHECK(s.taskbarMonitorMode == TaskbarMonitorMode::All && s.taskbarMonitorNumber == 3);
        CHECK(SettingsFieldValue(s, SettingsField::ShowOn) == -1);
        SettingsAssign(s, SettingsField::ShowDetailsOnHover, 0);
        SettingsAssign(s, SettingsField::ShowDetailsOnHover, 0);  // Desired, not toggled
        CHECK(!s.showDetailsOnHover);
        SettingsAssign(s, kPoll, 1);
        CHECK(s.pollMinutes == 2);
        // Kept forever by default: the custom box shows the most days and changes nothing.
        CHECK(s.historyRetentionDays == 0 && SettingsFieldValue(s, kKeep) == 0);
        CHECK(SettingsFieldValue(s, kRetention) == 3650);
        SettingsAssign(s, kRetention, 30);
        CHECK(s.historyRetentionDays == 0);
        SettingsAssign(s, kKeep, 99999);
        CHECK(s.historyRetentionDays == 3650);
        SettingsAssign(s, kRetention, 30);
        CHECK(s.historyRetentionDays == 30 && SettingsFieldValue(s, kKeep) == 30);
        SettingsAssign(s, kRetention, 99999);
        CHECK(s.historyRetentionDays == 3650);
        // The history window's default range: 7 days, choices clamped to 24 hours and All retained.
        CHECK(SettingsFieldValue(s, SettingsField::HistoryRange) == int(HistoryUiRange::Week));
        SettingsAssign(s, SettingsField::HistoryRange, int(HistoryUiRange::Month));
        CHECK(s.historyDefaultRange == HistoryUiRange::Month);
        SettingsAssign(s, SettingsField::HistoryRange, 99);
        CHECK(s.historyDefaultRange == HistoryUiRange::All);
        SettingsAssign(s, SettingsField::HistoryRange, -1);
        CHECK(s.historyDefaultRange == HistoryUiRange::Day);
        // Notification levels: choices clamped to Off and Red.
        CHECK(SettingsFieldValue(s, SettingsField::Notifications) == int(NotificationLevel::Red));
        SettingsAssign(s, SettingsField::Notifications, int(NotificationLevel::Yellow));
        CHECK(s.notificationLevel == NotificationLevel::Yellow);
        SettingsAssign(s, SettingsField::Notifications, 9);
        CHECK(s.notificationLevel == NotificationLevel::Red);
        SettingsAssign(s, SettingsField::Notifications, -1);
        CHECK(s.notificationLevel == NotificationLevel::Off);
        s.historyEnabled = false;
        SettingsAssign(s, SettingsField::ResetBehavior, 0);
        CHECK(s.taskbarMonitorMode == TaskbarMonitorMode::Primary && s.taskbarMonitorNumber == 1);
        CHECK(s.showDetailsOnHover && s.pollMinutes == 10 && s.notificationLevel == NotificationLevel::Red);
        CHECK(!s.historyEnabled && s.historyRetentionDays == 3650);  // Not Behavior's
        CHECK(s.historyDefaultRange == HistoryUiRange::Day);
        SettingsAssign(s, SettingsField::ResetHistory, 0);
        CHECK(s.historyEnabled && s.historyRetentionDays == 0 && s.historyDefaultRange == HistoryUiRange::Week);
    });

    Run("projection: valid drafts show; invalid, empty and overflowing text keeps the last valid value", [] {
        TestStore store;
        store.settings.historyRetentionDays = 120;
        SettingsEdits edits(store);
        edits.Reload();
        edits.SetText(0, kRetention, L"");
        CHECK(edits.Projection().historyRetentionDays == 120);
        edits.SetText(0, kRetention, L"3");
        edits.SetText(0, kRetention, L"36");
        CHECK(edits.Projection().historyRetentionDays == 36);
        edits.SetText(0, kRetention, L"36a");
        CHECK(edits.Projection().historyRetentionDays == 36);
        edits.SetText(0, kRetention, L"99999999999999999999");
        CHECK(edits.Projection().historyRetentionDays == 36);
        edits.SetText(0, kRetention, L"0");
        CHECK(edits.Projection().historyRetentionDays == 1);
        edits.SetText(0, kRetention, L"9,999");
        CHECK(edits.Projection().historyRetentionDays == 3650);
        CHECK(store.Applied().empty());
    });

    Run("controller: focus loss queues; a page reset then drops the request", [] {
        TestStore store;
        SettingsEdits edits(store);
        edits.Reload();
        edits.SetText(0, kRetention, L"30");
        CHECK(edits.RequestCommit(0, kRetention));  // Blur: queued, processed later
        CHECK(edits.Reset(SettingsField::ResetHistory) == 0);
        CHECK(edits.Process() == 0);
        CHECK(store.Applied() == std::vector<std::wstring>{Op(SettingsField::ResetHistory, 0)});
        CHECK(store.settings.historyRetentionDays == 0 && edits.drafts.empty());  // Forever
    });

    Run("controller: Esc after a deferred commit skips it", [] {
        TestStore store;
        SettingsEdits edits(store);
        edits.Reload();
        edits.SetText(0, kRetention, L"30");
        CHECK(edits.RequestCommit(0, kRetention));
        edits.Revert(0, kRetention);
        CHECK(edits.Process() == 0 && store.Applied().empty() && edits.drafts.empty());
        CHECK(edits.Projection().historyRetentionDays == 90);
    });

    Run("controller: a newer edit supersedes the queued request; its own commit applies", [] {
        TestStore store;
        SettingsEdits edits(store);
        edits.Reload();
        edits.SetText(0, kRetention, L"30");
        CHECK(edits.RequestCommit(0, kRetention));
        edits.SetText(0, kRetention, L"45");
        CHECK(edits.Process() == 0 && store.Applied().empty());
        CHECK(edits.drafts.size() == 1 && edits.drafts[0].raw == L"45");
        CHECK(edits.RequestCommit(0, kRetention) && edits.Process() == 0);
        CHECK(store.Applied() == std::vector<std::wstring>{Op(kRetention, 45)});
        CHECK(edits.drafts.empty() && edits.baseline.settings.historyRetentionDays == 45);
    });

    Run("controller: an external refresh reloads the baseline and keeps dirty drafts uncommitted", [] {
        TestStore store;
        SettingsEdits edits(store);
        edits.Reload();
        edits.SetText(0, kRetention, L"45");
        CHECK(edits.RequestCommit(0, kRetention));  // Queued, not yet processed
        store.settings.historyEnabled = false;      // The history window's consent change
        edits.Reload();
        CHECK(store.Applied().empty());
        CHECK(edits.drafts.size() == 1 && edits.queue.size() == 1);
        Settings projected = edits.Projection();
        CHECK(!projected.historyEnabled && projected.historyRetentionDays == 45);
    });

    Run("controller: a failed save keeps the draft and its error; only the next action retries", [] {
        TestStore store;
        SettingsEdits edits(store);
        edits.Reload();
        store.failNext = 1;
        edits.SetText(0, kRetention, L"30");
        CHECK(edits.RequestCommit(0, kRetention));
        CHECK(edits.Process() == 1);
        CHECK(edits.drafts.size() == 1 && edits.drafts[0].failed && !edits.drafts[0].error.empty());
        CHECK(edits.Projection().historyRetentionDays == 30);
        edits.Reload();  // Notifications don't retry
        CHECK(edits.Process() == 0 && store.applies == 1 && edits.drafts[0].failed);
        // Nor does focus leaving the box (the error message box takes focus, for one).
        CHECK(!edits.RequestCommit(0, kRetention, true) && edits.queue.empty());
        CHECK(edits.RequestCommit(0, kRetention) && edits.Process() == 0);  // Enter again
        CHECK(store.applies == 2 && edits.drafts.empty() && store.settings.historyRetentionDays == 30);
        // A discrete control keeps its desired value while failed.
        store.failNext = 1;
        CHECK(edits.Commit(0, SettingsField::Notifications, 0) == 1);
        CHECK(edits.Projection().notificationLevel == NotificationLevel::Off &&
              store.settings.notificationLevel == NotificationLevel::Red);
        CHECK(edits.Commit(0, SettingsField::Notifications, 0) == 0 &&
              store.settings.notificationLevel == NotificationLevel::Off);
    });

    Run("controller: an invalid draft gets its error and is never queued", [] {
        TestStore store;
        SettingsEdits edits(store);
        edits.Reload();
        edits.SetText(0, kPoll, L"12a");
        CHECK(!edits.RequestCommit(0, kPoll));
        CHECK(edits.drafts[0].error == L"Enter a whole number from 2 to 1440." && edits.queue.empty());
        edits.SetText(0, kPoll, L"12");  // Typing clears it
        CHECK(edits.drafts[0].error.empty());
    });

    Run("controller: a discrete choice supersedes a queued text draft of the same field", [] {
        TestStore store;
        SettingsEdits edits(store);
        edits.Reload();
        edits.SetText(0, kPoll, L"7");
        CHECK(edits.RequestCommit(0, kPoll));
        CHECK(edits.Commit(0, kPoll, 5) == 0);  // The preset dropdown
        CHECK(store.Applied() == std::vector<std::wstring>{Op(kPoll, 5)});
        CHECK(edits.drafts.empty() && store.settings.pollMinutes == 5);
    });

    Run("controller: Keep history and its custom days drop each other's drafts, failed ones included", [] {
        TestStore store;
        SettingsEdits edits(store);
        edits.Reload();
        // A preset whose save failed, then days typed: closing saves only the typed days.
        store.failNext = 1;
        CHECK(edits.Commit(0, kKeep, 30) == 1);
        CHECK(edits.Projection().historyRetentionDays == 30);
        edits.SetText(0, kRetention, L"180");
        CHECK(edits.drafts.size() == 1 && edits.drafts[0].field == kRetention);
        CHECK(edits.PrepareClose() == SettingsCloseResult::Saved);
        CHECK((store.Applied() == std::vector<std::wstring>{Op(kKeep, 30), Op(kRetention, 180)}));
        CHECK(store.settings.historyRetentionDays == 180);
        // Days typed and queued, then Forever: the typed days go with their request.
        edits.SetText(0, kRetention, L"45");
        CHECK(edits.RequestCommit(0, kRetention));
        CHECK(edits.Commit(0, kKeep, 0) == 0);
        CHECK(edits.drafts.empty() && edits.queue.empty() && store.settings.historyRetentionDays == 0);
        CHECK((store.Applied() == std::vector<std::wstring>{Op(kKeep, 30), Op(kRetention, 180), Op(kKeep, 0)}));
        // Kept forever, custom days that still arrive change nothing.
        edits.SetText(0, kRetention, L"45");
        CHECK(edits.Projection().historyRetentionDays == 0);
        CHECK(edits.RequestCommit(0, kRetention) && edits.Process() == 0);
        CHECK(edits.drafts.empty() && store.settings.historyRetentionDays == 0);
    });

    Run("controller: steps start from the last valid value, clamped, as text", [] {
        TestStore store;
        store.settings.historyRetentionDays = 3650;
        SettingsEdits edits(store);
        edits.Reload();
        edits.Step(0, kRetention, 1);
        CHECK(edits.drafts[0].raw == L"3,650" && edits.drafts[0].value == 3650);
        edits.SetText(0, kRetention, L"40x");
        edits.Step(0, kRetention, -1);
        CHECK(edits.drafts[0].raw == L"3,649" && edits.drafts[0].valid);
        CHECK(store.Applied().empty());
    });

    Run("controller: a step's debounced commit applies only the stepped revision", [] {
        TestStore store;
        store.settings.historyRetentionDays = 365;
        SettingsEdits edits(store);
        edits.Reload();
        uint64_t stepped = edits.Step(0, kRetention, 1);
        edits.SetText(0, kRetention, L"3");  // Typing "365" again before the timer runs out
        CHECK(!edits.CommitStep(0, kRetention, stepped) && edits.queue.empty());
        CHECK(edits.Process() == 0 && store.Applied().empty());
        stepped = edits.Step(0, kRetention, 1);
        CHECK(edits.drafts[0].raw == L"4" && edits.CommitStep(0, kRetention, stepped));
        CHECK(edits.Process() == 0 && store.Applied() == std::vector<std::wstring>{Op(kRetention, 4)});
        CHECK(!edits.CommitStep(0, kRetention, stepped));  // Done: no draft left
        CHECK(edits.Step(0, SettingsField::Notifications, 1) == 0);  // No range, no step
        // Step, Enter, the save fails: a stale timer for that revision doesn't retry it.
        stepped = edits.Step(0, kRetention, 1);
        store.failNext = 1;
        CHECK(edits.RequestCommit(0, kRetention) && edits.Process() == 1);
        CHECK(!edits.CommitStep(0, kRetention, stepped) && edits.queue.empty());
        CHECK(edits.drafts[0].failed && store.applies == 2);
    });

    Run("navigation: valid drafts commit, invalid ones revert unsaved, failed ones stay", [] {
        TestStore store;
        SettingsEdits edits(store);
        edits.Reload();
        edits.SetText(0, kPoll, L"12a");
        edits.SetText(0, kRetention, L"120");
        CHECK(edits.Navigate() == 0);
        CHECK(store.Applied() == std::vector<std::wstring>{Op(kRetention, 120)});
        CHECK(edits.drafts.empty() && store.settings.pollMinutes == 10);
        store.failNext = 1;
        edits.SetText(0, kRetention, L"150");
        CHECK(edits.Navigate() == 1);
        CHECK(edits.drafts.size() == 1 && edits.drafts[0].failed);
        CHECK(edits.Navigate() == 0 && store.applies == 2);  // Kept, not retried
    });

    Run("close preparation: every draft saves in order, one transaction each", [] {
        TestStore store;
        SettingsEdits edits(store);
        edits.Reload();
        edits.SetText(0, kRetention, L"30");
        edits.SetText(0, kPoll, L"7");
        CHECK(edits.RequestCommit(0, kPoll));  // Queued requests are covered by the flush
        CHECK(edits.PrepareClose() == SettingsCloseResult::Saved);
        CHECK((store.Applied() == std::vector<std::wstring>{Op(kRetention, 30), Op(kPoll, 7)}));
        CHECK(edits.drafts.empty() && edits.queue.empty());
    });

    Run("close preparation: a failing or invalid draft blocks; discard drops everything", [] {
        TestStore store;
        SettingsEdits edits(store);
        edits.Reload();
        store.failNext = 1;
        edits.SetText(0, kRetention, L"30");
        CHECK(edits.PrepareClose() == SettingsCloseResult::Blocked);
        CHECK(edits.drafts.size() == 1 && edits.drafts[0].failed);
        edits.SetText(0, kPoll, L"-");
        CHECK(edits.PrepareClose() == SettingsCloseResult::Blocked);  // Retention saves, poll blocks
        CHECK(edits.drafts.size() == 1 && edits.drafts[0].field == kPoll && !edits.drafts[0].error.empty());
        edits.Discard();
        CHECK(edits.drafts.empty() && edits.PrepareClose() == SettingsCloseResult::Saved);
        CHECK((store.Applied() == std::vector<std::wstring>{Op(kRetention, 30), Op(kRetention, 30)}));
    });

    Run("close preparation: unload between transactions stops the flush", [] {
        TestStore store;
        SettingsEdits edits(store);
        edits.Reload();
        edits.SetText(0, kRetention, L"30");
        edits.SetText(0, kPoll, L"7");
        store.unloadAfter = 1;
        CHECK(edits.PrepareClose() == SettingsCloseResult::Unloading);
        CHECK(store.Applied() == std::vector<std::wstring>{Op(kRetention, 30)});
        CHECK(edits.Process() == 0 && store.applies == 1);  // Nothing else commits once unloading
    });

    Run("page reset: drops only its own fields' drafts", [] {
        TestStore store;
        SettingsEdits edits(store);
        edits.Reload();
        edits.SetText(0, kRetention, L"30");
        edits.SetText(0, kPoll, L"7");
        CHECK(edits.Reset(SettingsField::ResetBehavior) == 0);
        CHECK(edits.drafts.size() == 1 && edits.drafts[0].field == kRetention);
    });
}

// Pages ------------------------------------------------------------------------------------------

static SettingsView TestView(SettingsUiPage page) {
    SettingsView view;
    view.page = page;
    view.settings = StackedSettings();
    view.status.displays = {{1, 2560, 1440, true}, {2, 1920, 1080, false}};
    view.status.historyFolder = L"C:\\Users\\clero\\AppData\\Local\\TaskbarAIQuota\\taskbar-ai-quota";
    return view;
}

static const UiItem& Control(const UiFrame& frame, SettingsField field, uint32_t part = 0) {
    const UiItem* item = UiFindItem(frame, SettingsControlId(field, part));
    if (!item) throw std::runtime_error("missing control " + std::to_string(int(field)));
    return *item;
}

static bool HasText(const UiFrame& frame, std::wstring_view text) {
    return std::any_of(frame.content.begin(), frame.content.end(), [&](const UiItem& item) {
        return UiRunsText(item.runs).find(text) != std::wstring::npos;
    });
}

static void RunPageTests() {
    Run("behavior page: controls follow the projection; displays; custom interval", [] {
        UiGraphics& g = TestGraphics();
        SettingsView view = TestView(SettingsUiPage::Behavior);
        UiFrame frame;
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        const UiItem& showOn = Control(frame, SettingsField::ShowOn);
        CHECK(showOn.kind == UiItemKind::Dropdown && showOn.text == L"Primary display only");
        CHECK_NEAR(showOn.rect.right - showOn.rect.left, 260);
        CHECK(showOn.options.size() == 4 && showOn.options[2].text == L"Display 1 - 2560x1440 (primary)" &&
              showOn.options[3].value == 2);
        const UiItem& poll = Control(frame, kPoll, 8);
        CHECK(poll.text == L"10 minutes" && poll.options.back().text == L"Custom");
        const UiItem& custom = Control(frame, kPoll);
        CHECK(custom.kind == UiItemKind::NumberBox && !custom.enabled && custom.text == L"10");
        CHECK(HasText(frame, L"Choose Custom in Cloud poll interval to edit."));
        for (SettingsField field : {SettingsField::ShowDetailsOnHover, SettingsField::SkipHiddenPolling}) {
            const UiItem& toggle = Control(frame, field);
            CHECK(toggle.kind == UiItemKind::Toggle && toggle.selected && toggle.action.value == 0);
        }
        // Threshold notifications: Off or a level, each with its threshold.
        const UiItem& notifications = Control(frame, SettingsField::Notifications);
        CHECK(notifications.kind == UiItemKind::Dropdown && notifications.text == L"Red (90%)" &&
              notifications.options.size() == 4);
        CHECK_NEAR(notifications.rect.right - notifications.rect.left, 200);
        const std::pair<PCWSTR, NotificationLevel> levels[] = {{L"Off", NotificationLevel::Off},
                                                               {L"Yellow (50%)", NotificationLevel::Yellow},
                                                               {L"Orange (75%)", NotificationLevel::Orange},
                                                               {L"Red (90%)", NotificationLevel::Red}};
        for (size_t i = 0; i < std::size(levels); i++) {
            CHECK(notifications.options[i].text == levels[i].first &&
                  notifications.options[i].value == int(levels[i].second));
        }
        CHECK(Control(frame, SettingsField::ResetBehavior).text == L"Reset");
        CHECK(HasText(frame, L"Notifies when a taskbar quota first reaches the chosen threshold."));
        // Focus order: language and display dropdowns, poll dropdown, (disabled box skipped),
        // toggles, notifications dropdown, reset.
        std::vector<UiFocusStop> stops = UiFocusStops(frame);
        CHECK(stops.size() == 6 + 7);

        // A chosen display that is gone stays chosen, marked unavailable; Custom enables the box
        // and its draft text and error show.
        view.settings.taskbarMonitorMode = TaskbarMonitorMode::Specific;
        view.settings.taskbarMonitorNumber = 3;
        view.settings.showDetailsOnHover = false;
        view.pollCustom = true;
        view.drafts.push_back({0, kPoll, 1, true, L"7x", 10, false, false,
                               L"Enter a whole number from 2 to 1440."});
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(Control(frame, SettingsField::ShowOn).text == L"Display 3 - unavailable");
        CHECK(Control(frame, kPoll, 8).text == L"Custom");
        CHECK(Control(frame, kPoll).enabled && Control(frame, kPoll).text == L"7x");
        CHECK(HasText(frame, L"Enter a whole number from 2 to 1440."));
        CHECK(!HasText(frame, L"Choose Custom"));
        const UiItem& hover = Control(frame, SettingsField::ShowDetailsOnHover);
        CHECK(!hover.selected && hover.text == L"Off" && hover.action.value == 1);
        // The control ids stay put when lines appear above them.
        CHECK(Control(frame, SettingsField::Notifications).kind == UiItemKind::Dropdown);

        // The levels follow the thresholds; Off shows as chosen.
        view = TestView(SettingsUiPage::Behavior);
        view.settings.yellowThreshold = 40;
        view.settings.orangeThreshold = 60;
        view.settings.redThreshold = 85;
        view.settings.notificationLevel = NotificationLevel::Off;
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        const UiItem& off = Control(frame, SettingsField::Notifications);
        CHECK(off.text == L"Off" && off.options[1].text == L"Yellow (40%)" &&
              off.options[2].text == L"Orange (60%)" && off.options[3].text == L"Red (85%)");

        // A saved custom interval being retyped: "1" projects to 2, a preset, yet Custom stays
        // and the box stays enabled.
        view = TestView(SettingsUiPage::Behavior);
        view.settings.pollMinutes = 7;
        view.drafts.push_back({0, kPoll, 1, true, L"1", 2, true, false, L""});
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(Control(frame, kPoll, 8).text == L"Custom" && Control(frame, kPoll).enabled);
    });

    Run("history page: status lines, retention presets, forever and custom, consent, links, folder", [] {
        UiGraphics& g = TestGraphics();
        SettingsView view = TestView(SettingsUiPage::History);
        UiFrame frame;
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(HasText(frame, L"\u25CF Recording \u00B7 New samples arrive with each poll."));
        CHECK(Control(frame, SettingsField::RecordHistory).selected);
        // Kept forever by default: the custom box shows the most days, disabled.
        const UiItem& keep = Control(frame, kKeep);
        CHECK(keep.kind == UiItemKind::Dropdown && keep.enabled && keep.text == L"Forever");
        CHECK(keep.options.size() == 5 && keep.options[2].text == L"1 year" && keep.options[2].value == 365 &&
              keep.options[4].text == L"Custom");
        CHECK(!Control(frame, kRetention).enabled && Control(frame, kRetention).text == L"3,650");
        CHECK(HasText(frame, L"Choose Custom in Keep history to edit."));
        CHECK(HasText(frame, L"keep history forever"));
        CHECK(Control(frame, kRetention, 3).runs[0].text == L"days");
        // A preset; days no preset has, or Custom chosen: Custom with its box enabled. Kept forever,
        // Custom chosen earlier enables nothing.
        struct RetentionCase {
            int days;
            bool custom;
            PCWSTR shown;
            bool enabled;
        };
        for (const RetentionCase& c : {RetentionCase{365, false, L"1 year", false}, RetentionCase{120, false, L"Custom", true},
                                       RetentionCase{90, true, L"Custom", true}, RetentionCase{0, true, L"Forever", false}}) {
            view.settings.historyRetentionDays = c.days;
            view.retentionCustom = c.custom;
            frame = {};
            SettingsUiBuildFrame(g, view, {1000, 820}, frame);
            CHECK(Control(frame, kKeep).text == c.shown && Control(frame, kRetention).enabled == c.enabled);
        }
        view.retentionCustom = false;
        CHECK(HasText(frame, view.status.historyFolder));
        CHECK(Control(frame, SettingsField::OpenHistory).text == L"Open usage history");
        // The default time span: 7 days by default, every range of the window, also while recording is off.
        const UiItem& span = Control(frame, SettingsField::HistoryRange);
        CHECK(span.kind == UiItemKind::Dropdown && span.enabled && span.text == L"7 days");
        CHECK(span.options.size() == 5 && span.options[0].text == L"24 hours" &&
              span.options[0].value == int(HistoryUiRange::Day) && span.options[4].text == L"All retained" &&
              span.options[4].value == int(HistoryUiRange::All));
        CHECK(HasText(frame, L"open on 7 days"));
        CHECK(Control(frame, SettingsField::OpenHistoryFolder).icon == L"\uE838");
        const UiItem& info = Control(frame, kRetention, 8);
        CHECK(UiLinkRun(info.runs, 0)->action.value == int(SettingsUiPage::Accounts));
        CHECK(UiLinkRun(info.runs, 1)->action.value == int(SettingsUiPage::Behavior));

        view.settings.historyEnabled = false;
        view.settings.historyRetentionDays = 1500;
        view.settings.historyDefaultRange = HistoryUiRange::Quarter;
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(HasText(frame, L"\u25CB Off"));
        CHECK(Control(frame, SettingsField::HistoryRange).enabled &&
              Control(frame, SettingsField::HistoryRange).text == L"90 days");
        CHECK(!Control(frame, kKeep).enabled && Control(frame, kKeep).text == L"Custom");
        CHECK(!Control(frame, kRetention).enabled && Control(frame, kRetention).text == L"1,500");
        CHECK(HasText(frame, L"Turn on Record usage history to change this."));
        view.settings.historyEnabled = true;
        for (auto [state, text] : {std::pair{SettingsRecording::PendingSave, L"\u25CF Waiting"},
                                   std::pair{SettingsRecording::StorageError, L"\u25CF Not saving"}}) {
            view.status.recording = state;
            frame = {};
            SettingsUiBuildFrame(g, view, {1000, 820}, frame);
            CHECK(HasText(frame, text));
        }
    });
}
