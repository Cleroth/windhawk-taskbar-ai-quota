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
    // Stored settings the load couldn't read: applies that change something and adds fail as
    // blocked replacements until ConfirmReplace ("confirm" in the log)
    bool replaceUnconfirmed = false;

    SettingsBaseline Load() override {
        log.push_back(L"load");
        return {settings, status};
    }
    SettingsSaveResult Apply(SettingsField field, int value, bool& replaceBlocked) override {
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
        if (changed && replaceUnconfirmed) {
            replaceBlocked = true;
            return SettingsSaveResult::Failed;
        }
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
            result.notice = SettingsNoticeCode::AccountSaveFailed;
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
                    result.notice = SettingsNoticeCode::HistoryKept;
                    result.reason = HistoryJournalError::CannotWrite;
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
                    result.notice = SettingsNoticeCode::AccountExists;
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
        if (failNext > 0 || replaceUnconfirmed) {
            result.replaceBlocked = failNext <= 0;
            if (failNext > 0) failNext--;
            log.push_back(L"add " + provider + L" failed");
            result.save = SettingsSaveResult::Failed;
            result.notice = SettingsNoticeCode::AccountSaveFailed;
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
        result.notice = SettingsNoticeCode::AccountExists;
        return result;
    }
    bool HasStoredSignIn(uint64_t identity) override {
        presenceChecks++;
        return std::find(signIns.begin(), signIns.end(), identity) != signIns.end();
    }
    void ConfirmReplace() override {
        log.push_back(L"confirm");
        replaceUnconfirmed = false;
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

// A draft's error as the window shows it in English.
static std::wstring ErrorText(const SettingsDraft& draft) { return SettingsDraftErrorText({}, draft); }

static std::wstring Op(SettingsField field, int value) {
    return std::to_wstring(int(field)) + L"=" + std::to_wstring(value);
}

static constexpr SettingsField kRetention = SettingsField::RetentionDays;
static constexpr SettingsField kKeep = SettingsField::KeepHistory;
static constexpr SettingsField kPoll = SettingsField::PollMinutes;
static constexpr SettingsField kHidden = SettingsField::HiddenPolling;
static constexpr SettingsField kHiddenMinutes = SettingsField::HiddenPollMinutes;

static void RunEditTests() {
    Run("parse: spaces, sign, digit grouping; empty, malformed and overflow fail; range clamps", [] {
        SettingsNumberRange days{1, 3650};
        // en-US groups (threes) with the separator given.
        auto parse = [&](std::wstring_view text, wchar_t separator = L',') {
            I18nNumberFormat format;
            format.group = {separator};
            SettingsParsedNumber parsed = SettingsParseNumber(text, days, format);
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
        I18nNumberFormat dots;
        dots.group = {L'.'};
        CHECK(I18nInteger(I18nLocale(), 1440) == L"1,440");
        CHECK(I18nInteger(I18nLocale(), 90) == L"90");
        CHECK(I18nInteger(I18nLocale(kI18nLanguages[0], dots), 1234567) == L"1.234.567");
        CHECK(I18nInteger(I18nLocale(), 0) == L"0");
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
        // Hidden accounts: every hour by default. The choice takes an interval of 2 to 1440
        // minutes, not polled (0) or as visible ones (-1); the custom box sets an interval
        // whatever the choice, and shows the default one without one.
        CHECK(s.hiddenPollMinutes == 60 && SettingsFieldValue(s, kHidden) == 60 && SettingsFieldValue(s, kHiddenMinutes) == 60);
        CHECK(!SettingsFieldRange(kHidden) && SettingsFieldRange(kHiddenMinutes)->minimum == 2 &&
              SettingsFieldRange(kHiddenMinutes)->maximum == 1440);
        SettingsAssign(s, kHidden, 240);
        CHECK(s.hiddenPollMinutes == 240 && SettingsFieldValue(s, kHidden) == 240 && SettingsFieldValue(s, kHiddenMinutes) == 240);
        SettingsAssign(s, kHiddenMinutes, 45);
        CHECK(s.hiddenPollMinutes == 45 && SettingsFieldValue(s, kHidden) == 45);
        SettingsAssign(s, kHiddenMinutes, 1);
        CHECK(s.hiddenPollMinutes == 2);
        SettingsAssign(s, kHiddenMinutes, 99999);
        CHECK(s.hiddenPollMinutes == 1440);
        SettingsAssign(s, kHidden, 1);
        CHECK(s.hiddenPollMinutes == 2);
        SettingsAssign(s, kHidden, 99999);
        CHECK(s.hiddenPollMinutes == 1440);
        for (int choice : {kHiddenPollNever, kHiddenPollAsShown}) {
            SettingsAssign(s, kHidden, choice);
            CHECK(s.hiddenPollMinutes == choice && SettingsFieldValue(s, kHidden) == choice);
            CHECK(SettingsFieldValue(s, kHiddenMinutes) == 60);
            SettingsAssign(s, kHiddenMinutes, 45);
            CHECK(s.hiddenPollMinutes == 45);
        }
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
        // Work days: off, Monday (1) to Friday (5) by default; the switch as desired, the days
        // clamped to Sunday (0) and Saturday (6).
        CHECK(SettingsFieldValue(s, SettingsField::WorkDays) == 0 && SettingsFieldValue(s, SettingsField::FirstWorkDay) == 1 &&
              SettingsFieldValue(s, SettingsField::LastWorkDay) == 5);
        SettingsAssign(s, SettingsField::WorkDays, 7);
        SettingsAssign(s, SettingsField::FirstWorkDay, -1);
        SettingsAssign(s, SettingsField::LastWorkDay, INT_MAX);
        CHECK(s.workDays && s.firstWorkDay == 0 && s.lastWorkDay == 6 && SettingsFieldValue(s, SettingsField::WorkDays) == 1);
        SettingsAssign(s, SettingsField::FirstWorkDay, 9);
        SettingsAssign(s, SettingsField::LastWorkDay, 4);
        CHECK(s.firstWorkDay == 6 && SettingsFieldValue(s, SettingsField::LastWorkDay) == 4);
        CHECK(!SettingsFieldRange(SettingsField::FirstWorkDay) && !SettingsFieldRange(SettingsField::LastWorkDay));
        s.historyEnabled = false;
        SettingsAssign(s, SettingsField::ResetBehavior, 0);
        CHECK(s.taskbarMonitorMode == TaskbarMonitorMode::Primary && s.taskbarMonitorNumber == 1);
        CHECK(s.showDetailsOnHover && s.pollMinutes == 10 && s.notificationLevel == NotificationLevel::Red);
        CHECK(s.hiddenPollMinutes == 60);
        CHECK(!s.workDays && s.firstWorkDay == 1 && s.lastWorkDay == 5);
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
        CHECK(edits.drafts.size() == 1 && edits.drafts[0].failed && !ErrorText(edits.drafts[0]).empty());
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
        CHECK(ErrorText(edits.drafts[0]) == L"Enter a whole number from 2 to 1440." && edits.queue.empty());
        edits.SetText(0, kPoll, L"12");  // Typing clears it
        CHECK(ErrorText(edits.drafts[0]).empty());
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

    Run("controller: Hidden accounts and its custom minutes drop each other's drafts, failed ones included; the "
        "minutes make any choice an interval", [] {
        TestStore store;
        SettingsEdits edits(store);
        edits.Reload();
        // A preset whose save failed, then minutes typed: closing saves only the typed minutes.
        store.failNext = 1;
        CHECK(edits.Commit(0, kHidden, 240) == 1);
        CHECK(edits.Projection().hiddenPollMinutes == 240 && store.settings.hiddenPollMinutes == 60);
        edits.SetText(0, kHiddenMinutes, L"90");
        CHECK(edits.drafts.size() == 1 && edits.drafts[0].field == kHiddenMinutes);
        CHECK(edits.Projection().hiddenPollMinutes == 90);
        CHECK(edits.PrepareClose() == SettingsCloseResult::Saved);
        CHECK((store.Applied() == std::vector<std::wstring>{Op(kHidden, 240), Op(kHiddenMinutes, 90)}));
        CHECK(store.settings.hiddenPollMinutes == 90);
        // Typed minutes project and save clamped to 2 to 1440.
        edits.SetText(0, kHiddenMinutes, L"1");
        CHECK(edits.Projection().hiddenPollMinutes == 2);
        edits.SetText(0, kHiddenMinutes, L"5,000");
        CHECK(edits.Projection().hiddenPollMinutes == 1440);
        // Minutes typed and queued, then each choice that isn't an interval: the typed minutes go
        // with their request. Minutes that arrive after it make an interval of it.
        for (int choice : {kHiddenPollNever, kHiddenPollAsShown}) {
            edits.SetText(0, kHiddenMinutes, L"45");
            CHECK(edits.RequestCommit(0, kHiddenMinutes));
            CHECK(edits.Commit(0, kHidden, choice) == 0);
            CHECK(edits.drafts.empty() && edits.queue.empty() && store.settings.hiddenPollMinutes == choice);
            CHECK(store.Applied().back() == Op(kHidden, choice));
            // A step starts from the default interval the box shows.
            edits.Step(0, kHiddenMinutes, 1);
            CHECK(edits.drafts[0].raw == L"61" && edits.Projection().hiddenPollMinutes == 61);
            edits.Revert(0, kHiddenMinutes);
            CHECK(edits.Projection().hiddenPollMinutes == choice);
            edits.SetText(0, kHiddenMinutes, L"45");
            CHECK(edits.Projection().hiddenPollMinutes == 45);
            CHECK(edits.RequestCommit(0, kHiddenMinutes) && edits.Process() == 0);
            CHECK(edits.drafts.empty() && store.settings.hiddenPollMinutes == 45);
        }
        // A choice again: its queued request goes when minutes are typed after it, and they show.
        edits.SetValue(0, kHidden, 120);
        CHECK(edits.RequestCommit(0, kHidden));
        edits.SetText(0, kHiddenMinutes, L"50");
        CHECK(edits.drafts.size() == 1 && edits.drafts[0].field == kHiddenMinutes && edits.queue.empty());
        CHECK(edits.Projection().hiddenPollMinutes == 50);
        edits.Discard();
        // Another field's draft stays through either one's edit.
        edits.SetText(0, kPoll, L"7");
        CHECK(edits.Commit(0, kHidden, 30) == 0 && store.settings.hiddenPollMinutes == 30);
        edits.SetText(0, kHiddenMinutes, L"31");
        CHECK(edits.drafts.size() == 2 && edits.drafts[0].field == kPoll && edits.drafts[0].raw == L"7");
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
        CHECK(edits.drafts.size() == 1 && edits.drafts[0].field == kPoll && !ErrorText(edits.drafts[0]).empty());
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

    // Stored settings the load couldn't read (the store's replaceUnconfirmed). The window asks on a
    // ReplaceBlocked notice and passes the answer to AnswerReplace; a close ends the question as No.
    Run("unreadable stored settings: only a blocked save asks; yes confirms and saves it again on fresh settings", [] {
        TestStore store;
        SettingsEdits edits(store);
        edits.Reload();
        // An ordinary failure doesn't ask.
        store.failNext = 1;
        CHECK(edits.Commit(0, SettingsField::Notifications, 0) == 1);
        CHECK(edits.notices.empty() && edits.replaceBlocked.empty() &&
              edits.drafts[0].error == SettingsDraftError::SaveFailed);
        // A save that would replace them fails with its own error and one question.
        store.replaceUnconfirmed = true;
        CHECK(edits.Commit(0, SettingsField::Notifications, 0) == 1);
        CHECK(edits.notices.size() == 1 && edits.notices[0].code == SettingsNoticeCode::ReplaceBlocked &&
              edits.notices[0].failure && edits.replaceBlocked.size() == 1);
        CHECK(edits.drafts.size() == 1 && edits.drafts[0].failed &&
              ErrorText(edits.drafts[0]) == L"Not saved: saving would replace the stored settings, which couldn't be read.");
        CHECK(store.settings.notificationLevel == NotificationLevel::Red);
        // Yes, with the settings changed meanwhile: confirmed, then saved on them.
        store.settings.pollMinutes = 30;
        edits.notices.clear();
        CHECK(edits.AnswerReplace(true) == 0);
        CHECK((std::vector<std::wstring>(store.log.end() - 3, store.log.end()) ==
               std::vector<std::wstring>{L"confirm", L"apply " + Op(SettingsField::Notifications, 0), L"load"}));
        CHECK(edits.drafts.empty() && edits.replaceBlocked.empty() && edits.notices.empty());
        CHECK(store.settings.notificationLevel == NotificationLevel::Off && edits.baseline.settings.pollMinutes == 30);
        // Later saves go ahead without asking.
        CHECK(edits.Commit(0, kPoll, 15) == 0 && edits.notices.empty() && store.settings.pollMinutes == 15);
    });

    Run("unreadable stored settings: no keeps the change failed until it is changed again; a newer draft "
        "isn't saved by the answer", [] {
        TestStore store;
        store.replaceUnconfirmed = true;
        SettingsEdits edits(store);
        edits.Reload();
        edits.SetText(0, kRetention, L"30");
        CHECK(edits.RequestCommit(0, kRetention) && edits.Process() == 1);
        CHECK(edits.notices.size() == 1 && edits.replaceBlocked.size() == 1);
        edits.notices.clear();
        CHECK(edits.AnswerReplace(false) == 0);
        CHECK(edits.replaceBlocked.empty() && edits.drafts.size() == 1 && edits.drafts[0].failed &&
              edits.drafts[0].error == SettingsDraftError::ReplaceBlocked);
        CHECK(std::count(store.log.begin(), store.log.end(), L"confirm") == 0 && store.applies == 1 &&
              store.settings.historyRetentionDays == 90);
        // Focus loss doesn't retry it; acting on it again asks again.
        CHECK(!edits.RequestCommit(0, kRetention, true));
        CHECK(edits.RequestCommit(0, kRetention) && edits.Process() == 1 && edits.notices.size() == 1 &&
              edits.replaceBlocked.size() == 1);
        // Typed again before the answer: yes confirms, and the newer draft waits for its own commit.
        edits.notices.clear();
        edits.SetText(0, kRetention, L"45");
        CHECK(edits.AnswerReplace(true) == 0 && store.applies == 2 && store.settings.historyRetentionDays == 90);
        CHECK(edits.drafts.size() == 1 && !edits.drafts[0].failed && edits.drafts[0].raw == L"45");
        CHECK(edits.RequestCommit(0, kRetention) && edits.Process() == 0 && edits.notices.empty());
        CHECK(store.settings.historyRetentionDays == 45 && edits.drafts.empty());
    });

    Run("unreadable stored settings: a close during the question keeps the change failed; unloading "
        "confirms nothing", [] {
        TestStore store;
        store.replaceUnconfirmed = true;
        SettingsEdits edits(store);
        edits.Reload();
        CHECK(edits.Commit(0, SettingsField::Notifications, 0) == 1 && edits.replaceBlocked.size() == 1);
        edits.notices.clear();
        CHECK(edits.AnswerReplace(false) == 0);  // The close's answer
        // Its flush meets the same block; the window asks nothing while closing, then offers to discard.
        CHECK(edits.PrepareClose() == SettingsCloseResult::Blocked);
        CHECK(edits.drafts.size() == 1 && edits.drafts[0].error == SettingsDraftError::ReplaceBlocked);
        CHECK(edits.notices.size() == 1 && edits.notices[0].code == SettingsNoticeCode::ReplaceBlocked &&
              edits.replaceBlocked.size() == 1);
        edits.notices.clear();
        CHECK(edits.AnswerReplace(false) == 0);
        edits.Discard();
        CHECK(edits.drafts.empty() && edits.PrepareClose() == SettingsCloseResult::Saved);
        // Unloading while the question shows: a yes confirms and saves nothing.
        CHECK(edits.Commit(0, SettingsField::Notifications, 0) == 1 && edits.replaceBlocked.size() == 1);
        edits.notices.clear();
        store.unloadAfter = 0;
        CHECK(edits.AnswerReplace(true) == 0 && edits.replaceBlocked.empty());
        CHECK(std::count(store.log.begin(), store.log.end(), L"confirm") == 0 && store.replaceUnconfirmed &&
              store.settings.notificationLevel == NotificationLevel::Red);
    });

    Run("unreadable stored settings: a blocked add or page reset asks instead of its own notice; yes adds "
        "anew and resets", [] {
        TestStore store;
        store.settings.pollMinutes = 30;
        SettingsEdits edits(store);
        edits.Reload();
        store.failNext = 1;  // An ordinary failed add says so and doesn't ask
        CHECK(edits.AddAccount(L"openai") == 0 && edits.notices.size() == 1 &&
              edits.notices[0].code == SettingsNoticeCode::AccountSaveFailed && edits.replaceBlocked.empty());
        edits.notices.clear();
        store.replaceUnconfirmed = true;
        CHECK(edits.AddAccount(L"openai") == 0);
        CHECK(edits.Reset(SettingsField::ResetBehavior) == 1);
        CHECK(edits.notices.size() == 2 && edits.replaceBlocked.size() == 2);
        for (const SettingsNotice& notice : edits.notices) CHECK(notice.code == SettingsNoticeCode::ReplaceBlocked);
        CHECK(store.settings.accounts.empty() && store.settings.pollMinutes == 30);
        edits.notices.clear();
        uint64_t added = 0;
        CHECK(edits.AnswerReplace(true, &added) == 0 && edits.notices.empty());
        CHECK(store.settings.accounts.size() == 1 && store.settings.accounts[0].label == L"O");
        CHECK(added && edits.IndexOf(added) == 0 && edits.pendingSignIn == added);
        CHECK(store.settings.pollMinutes == 10 && edits.baseline.settings.pollMinutes == 10);
    });

    Run("page reset: drops only its own fields' drafts; Behavior's holds the hidden accounts' choice and its "
        "custom minutes", [] {
        TestStore store;
        store.settings.hiddenPollMinutes = 90;
        SettingsEdits edits(store);
        edits.Reload();
        edits.SetText(0, kRetention, L"30");
        edits.SetText(0, kPoll, L"7");
        edits.SetText(0, kHiddenMinutes, L"45");
        CHECK(edits.RequestCommit(0, kHiddenMinutes));
        CHECK(edits.Reset(SettingsField::ResetBehavior) == 0);
        CHECK(edits.drafts.size() == 1 && edits.drafts[0].field == kRetention);
        CHECK(store.Applied() == std::vector<std::wstring>{Op(SettingsField::ResetBehavior, 0)});
        CHECK(store.settings.hiddenPollMinutes == 60 && store.settings.pollMinutes == 10);
        // A choice's draft goes the same way.
        store.failNext = 1;
        CHECK(edits.Commit(0, kHidden, kHiddenPollNever) == 1 && edits.drafts.size() == 2);
        CHECK(edits.Reset(SettingsField::ResetBehavior) == 0);
        CHECK(edits.drafts.size() == 1 && edits.drafts[0].field == kRetention && store.settings.hiddenPollMinutes == 60);
        std::vector<SettingsField> behavior = SettingsGroupFields(SettingsField::ResetBehavior);
        CHECK(behavior.size() == 10 && std::count(behavior.begin(), behavior.end(), kHidden) == 1 &&
              std::count(behavior.begin(), behavior.end(), kHiddenMinutes) == 1);
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

static bool Shown(const UiFrame& frame, SettingsField field, uint32_t part = 0) {
    return UiFindItem(frame, SettingsControlId(field, part)) != nullptr;
}

static bool HasText(const UiFrame& frame, std::wstring_view text) {
    return std::any_of(frame.content.begin(), frame.content.end(), [&](const UiItem& item) {
        return UiRunsText(item.runs).find(text) != std::wstring::npos;
    });
}

static int TextCount(const UiFrame& frame, std::wstring_view text) {
    return int(std::count_if(frame.content.begin(), frame.content.end(),
                             [&](const UiItem& item) { return UiRunsText(item.runs) == text; }));
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
        // A preset: no custom box.
        CHECK(!Shown(frame, kPoll) && TextCount(frame, L"Custom interval") == 0);
        const UiItem& toggle = Control(frame, SettingsField::ShowDetailsOnHover);
        CHECK(toggle.kind == UiItemKind::Toggle && toggle.selected && toggle.action.value == 0);
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
        CHECK(HasText(frame, L"Notifies when a taskbar quota first reaches the chosen threshold, and again at its "
                             L"limit."));
        // Focus order: language and display dropdowns, the hover toggle, the work days switch (its
        // days hidden), the poll and hidden accounts dropdowns (each one's custom box hidden),
        // notifications dropdown, reset.
        std::vector<UiFocusStop> stops = UiFocusStops(frame);
        CHECK(stops.size() == 6 + 8);
        const SettingsField order[] = {SettingsField::Language, SettingsField::ShowOn, SettingsField::ShowDetailsOnHover,
                                       SettingsField::WorkDays, kPoll, kHidden, SettingsField::Notifications,
                                       SettingsField::ResetBehavior};
        for (size_t i = 0; i < std::size(order); i++) {
            CHECK(stops[6 + i].ref.id == SettingsControlId(order[i], order[i] == kPoll ? 8 : 0));
        }

        // A chosen display that is gone stays chosen, marked unavailable; Custom shows the box
        // under its dropdown, and its draft text and error show.
        view.settings.taskbarMonitorMode = TaskbarMonitorMode::Specific;
        view.settings.taskbarMonitorNumber = 3;
        view.settings.showDetailsOnHover = false;
        view.pollCustom = true;
        view.drafts.push_back({0, kPoll, 1, true, L"7x", 10, false, false, SettingsDraftError::Range});
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(Control(frame, SettingsField::ShowOn).text == L"Display 3 - unavailable");
        CHECK(Control(frame, kPoll, 8).text == L"Custom");
        CHECK(Control(frame, kPoll).kind == UiItemKind::NumberBox && Control(frame, kPoll).enabled &&
              Control(frame, kPoll).text == L"7x" && TextCount(frame, L"Custom interval") == 1);
        CHECK(Control(frame, kPoll, 8).rect.bottom < Control(frame, kPoll).rect.top &&
              Control(frame, kPoll).rect.bottom < Control(frame, kHidden).rect.top);
        CHECK(HasText(frame, L"Enter a whole number from 2 to 1440."));
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
        // and so does the box.
        view = TestView(SettingsUiPage::Behavior);
        view.settings.pollMinutes = 7;
        view.drafts.push_back({0, kPoll, 1, true, L"1", 2, true, false});
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(Control(frame, kPoll, 8).text == L"Custom" && Control(frame, kPoll).enabled);
    });

    Run("behavior page: work days: the switch, then, while it is on, the first and last day from Monday on; they "
        "save at once and the Behavior reset restores them", [] {
        UiGraphics& g = TestGraphics();
        auto build = [&](const SettingsView& view) {
            UiFrame frame;
            SettingsUiBuildFrame(g, view, {1000, 820}, frame);
            return frame;
        };
        // Off by default: the switch alone, the poll interval's section after it.
        SettingsView view = TestView(SettingsUiPage::Behavior);
        UiFrame frame = build(view);
        CHECK(Control(frame, SettingsField::WorkDays).kind == UiItemKind::Toggle &&
              !Control(frame, SettingsField::WorkDays).selected && Control(frame, SettingsField::WorkDays).enabled);
        CHECK(!Shown(frame, SettingsField::FirstWorkDay) && !Shown(frame, SettingsField::LastWorkDay) &&
              !HasText(frame, L"First work day") && !HasText(frame, L"Last work day"));
        CHECK(HasText(frame, L"Pace and projection") && HasText(frame, L"Work days only") &&
              Control(frame, SettingsField::WorkDays).rect.bottom < Control(frame, kPoll, 8).rect.top);
        // A day whose save failed stays, with its error, while the switch is off.
        view.drafts.push_back({0, SettingsField::LastWorkDay, 1, false, L"", 5, true, true, SettingsDraftError::SaveFailed});
        frame = build(view);
        CHECK(!Shown(frame, SettingsField::FirstWorkDay) && Control(frame, SettingsField::LastWorkDay).enabled &&
              TextCount(frame, L"This change couldn't be saved.") == 1);
        view.drafts.clear();
        // On: Monday to Friday, each editable.
        view.settings.workDays = true;
        frame = build(view);
        const UiItem& toggle = Control(frame, SettingsField::WorkDays);
        const UiItem& first = Control(frame, SettingsField::FirstWorkDay);
        const UiItem& last = Control(frame, SettingsField::LastWorkDay);
        CHECK(toggle.selected && toggle.action.value == 0);
        CHECK(first.kind == UiItemKind::Dropdown && first.enabled && first.text == L"Monday" && first.action.value == 1);
        CHECK(last.kind == UiItemKind::Dropdown && last.enabled && last.text == L"Friday" && last.action.value == 5);
        // Both list the week from Monday, each day by its weekday number (Sunday 0).
        const std::pair<PCWSTR, int> week[] = {{L"Monday", 1}, {L"Tuesday", 2}, {L"Wednesday", 3}, {L"Thursday", 4},
                                               {L"Friday", 5}, {L"Saturday", 6}, {L"Sunday", 0}};
        CHECK(first.options.size() == 7 && last.options.size() == 7);
        for (size_t day = 0; day < 7 && first.options.size() == 7 && last.options.size() == 7; day++) {
            CHECK(first.options[day].text == week[day].first && first.options[day].value == week[day].second);
            CHECK(last.options[day].text == week[day].first && last.options[day].value == week[day].second);
        }
        // Their section sits between hover details and the poll interval, the switch first.
        CHECK(Control(frame, SettingsField::ShowDetailsOnHover).rect.bottom < toggle.rect.top &&
              toggle.rect.bottom < first.rect.top && first.rect.bottom < last.rect.top &&
              last.rect.bottom < Control(frame, kPoll, 8).rect.top);
        CHECK(HasText(frame, L"Pace and projection") && HasText(frame, L"Work days only") &&
              HasText(frame, L"First work day") && HasText(frame, L"Last work day") &&
              HasText(frame, L"Your work days run from this day through the last one.") &&
              HasText(frame, L"Language, displays, hover details, work days, polling and notifications."));

        // Sunday to Thursday: saved at once and shown.
        TestStore store;
        SettingsEdits edits(store);
        edits.Reload();
        CHECK(edits.Commit(0, SettingsField::WorkDays, 1) == 0 && edits.Commit(0, SettingsField::FirstWorkDay, 0) == 0 &&
              edits.Commit(0, SettingsField::LastWorkDay, 4) == 0);
        CHECK((store.Applied() == std::vector<std::wstring>{Op(SettingsField::WorkDays, 1), Op(SettingsField::FirstWorkDay, 0),
                                                            Op(SettingsField::LastWorkDay, 4)}));
        CHECK(store.settings.workDays && store.settings.firstWorkDay == 0 && store.settings.lastWorkDay == 4 &&
              edits.drafts.empty());
        view.settings = edits.Projection();
        frame = build(view);
        CHECK(Control(frame, SettingsField::WorkDays).selected);
        CHECK(Control(frame, SettingsField::FirstWorkDay).enabled && Control(frame, SettingsField::FirstWorkDay).text == L"Sunday");
        CHECK(Control(frame, SettingsField::LastWorkDay).enabled && Control(frame, SettingsField::LastWorkDay).text == L"Thursday");
        // Off again keeps the days; the Display reset leaves them, the Behavior reset restores them.
        CHECK(edits.Commit(0, SettingsField::WorkDays, 0) == 0);
        CHECK(!store.settings.workDays && store.settings.firstWorkDay == 0 && store.settings.lastWorkDay == 4);
        CHECK(edits.Reset(SettingsField::ResetDisplay) == 0);
        CHECK(store.settings.firstWorkDay == 0 && store.settings.lastWorkDay == 4);
        CHECK(edits.Commit(0, SettingsField::WorkDays, 1) == 0 && edits.Reset(SettingsField::ResetBehavior) == 0);
        CHECK(!store.settings.workDays && store.settings.firstWorkDay == 1 && store.settings.lastWorkDay == 5);
    });

    Run("behavior page: Hidden accounts' choices under the poll interval's cards; its custom box only while Custom "
        "shows; typed text and errors", [] {
        UiGraphics& g = TestGraphics();
        auto build = [&](const SettingsView& view) {
            UiFrame frame;
            SettingsUiBuildFrame(g, view, {1000, 820}, frame);
            return frame;
        };
        // Every hour by default, a preset: no custom box.
        SettingsView view = TestView(SettingsUiPage::Behavior);
        UiFrame frame = build(view);
        const UiItem& choice = Control(frame, kHidden);
        CHECK(choice.kind == UiItemKind::Dropdown && choice.enabled && choice.text == L"1 hour" && choice.action.value == 60);
        const std::pair<PCWSTR, int> options[] = {{L"Not polled", 0}, {L"Same as visible accounts", -1},
                                                  {L"30 minutes", 30},  {L"1 hour", 60},
                                                  {L"2 hours", 120},    {L"4 hours", 240},
                                                  {L"12 hours", 720},   {L"24 hours", 1440},
                                                  {L"Custom", kSettingsHiddenPollCustom}};
        CHECK(choice.options.size() == std::size(options));
        for (size_t i = 0; i < std::size(options); i++) {
            CHECK(choice.options[i].text == options[i].first && choice.options[i].value == options[i].second);
        }
        // Custom's value is none a stored choice takes.
        CHECK(kSettingsHiddenPollCustom < 0 && kSettingsHiddenPollCustom != kHiddenPollAsShown);
        CHECK_NEAR(choice.rect.right - choice.rect.left,
                   std::max(200.0f, TestTextWidth(L"Same as visible accounts", UiFont::Body) + 41));
        CHECK(!Shown(frame, kHiddenMinutes) && TextCount(frame, L"Hidden accounts") == 1 &&
              TextCount(frame, L"Custom hidden interval") == 0);
        CHECK(HasText(frame, L"How often accounts hidden from the taskbar are polled, for Accounts and usage history: "
                             L"never more often than visible ones. Shared across Windows users."));
        CHECK(Control(frame, kPoll, 8).rect.bottom < choice.rect.top &&
              choice.rect.bottom < Control(frame, SettingsField::Notifications).rect.top);

        struct HiddenCase {
            int minutes;   // The setting
            bool custom;   // Custom chosen earlier
            PCWSTR shown;  // The dropdown's text
            int value;     // The dropdown's value
            PCWSTR box;    // The custom box's text; none: no box
        };
        for (const HiddenCase& c : {HiddenCase{kHiddenPollNever, false, L"Not polled", 0, nullptr},
                                    HiddenCase{kHiddenPollAsShown, false, L"Same as visible accounts", -1, nullptr},
                                    HiddenCase{30, false, L"30 minutes", 30, nullptr},
                                    HiddenCase{1440, false, L"24 hours", 1440, nullptr},
                                    // Minutes no preset has, or Custom chosen: Custom, with its box.
                                    HiddenCase{90, false, L"Custom", kSettingsHiddenPollCustom, L"90"},
                                    HiddenCase{240, true, L"Custom", kSettingsHiddenPollCustom, L"240"},
                                    HiddenCase{1440, true, L"Custom", kSettingsHiddenPollCustom, L"1,440"},
                                    // Without an interval, Custom chosen earlier shows none.
                                    HiddenCase{kHiddenPollNever, true, L"Not polled", 0, nullptr},
                                    HiddenCase{kHiddenPollAsShown, true, L"Same as visible accounts", -1, nullptr}}) {
            view.settings.hiddenPollMinutes = c.minutes;
            view.hiddenPollCustom = c.custom;
            frame = build(view);
            CHECK(Control(frame, kHidden).text == c.shown && Control(frame, kHidden).action.value == c.value);
            CHECK(Shown(frame, kHiddenMinutes) == bool(c.box) && TextCount(frame, L"Custom hidden interval") == bool(c.box));
            if (c.box) {
                const UiItem& box = Control(frame, kHiddenMinutes);
                CHECK(box.kind == UiItemKind::NumberBox && box.enabled && box.text == c.box &&
                      Control(frame, kHiddenMinutes, 3).runs[0].text == L"minutes");
                CHECK(Control(frame, kHidden).rect.bottom < box.rect.top &&
                      box.rect.bottom < Control(frame, SettingsField::Notifications).rect.top);
            }
            // The box is a tab stop after its dropdown.
            std::vector<UiFocusStop> stops = UiFocusStops(frame);
            CHECK(stops.size() == size_t(6 + 8 + bool(c.box)));
            CHECK(stops[6 + 5].ref.id == SettingsControlId(kHidden) &&
                  (stops[6 + 6].ref.id == SettingsControlId(kHiddenMinutes)) == bool(c.box));
        }

        // A saved custom interval being retyped: "30" projects to a preset, yet Custom stays and
        // so does the box; text that isn't a number shows with its error, on this card only.
        view = TestView(SettingsUiPage::Behavior);
        view.settings.hiddenPollMinutes = 30;
        view.drafts.push_back({0, kHiddenMinutes, 1, true, L"30", 30, true, false});
        frame = build(view);
        CHECK(Control(frame, kHidden).text == L"Custom" && Control(frame, kHiddenMinutes).enabled &&
              Control(frame, kHiddenMinutes).text == L"30");
        view.drafts[0] = {0, kHiddenMinutes, 2, true, L"30x", 30, false, false, SettingsDraftError::Range};
        frame = build(view);
        CHECK(Control(frame, kHiddenMinutes).text == L"30x" && TextCount(frame, L"Enter a whole number from 2 to 1440.") == 1);
        CHECK(Control(frame, kPoll, 8).text == L"10 minutes" && !Shown(frame, kPoll));
        // The poll interval's Custom shows no box for hidden accounts, and the other way.
        view = TestView(SettingsUiPage::Behavior);
        view.pollCustom = true;
        frame = build(view);
        CHECK(Control(frame, kPoll).enabled && !Shown(frame, kHiddenMinutes) && Control(frame, kHidden).text == L"1 hour");
        // A choice whose save failed shows as chosen, with its error.
        view = TestView(SettingsUiPage::Behavior);
        view.settings.hiddenPollMinutes = kHiddenPollNever;
        view.drafts.push_back({0, kHidden, 1, false, L"", 0, true, true, SettingsDraftError::SaveFailed});
        frame = build(view);
        CHECK(Control(frame, kHidden).text == L"Not polled" && TextCount(frame, L"This change couldn't be saved.") == 1);
    });

    Run("history page: status lines, retention presets, forever and custom, consent, links, folder", [] {
        UiGraphics& g = TestGraphics();
        SettingsView view = TestView(SettingsUiPage::History);
        UiFrame frame;
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(HasText(frame, L"\u25CF Recording \u00B7 New samples arrive with each poll."));
        CHECK(Control(frame, SettingsField::RecordHistory).selected);
        // Kept forever by default: no custom box.
        const UiItem& keep = Control(frame, kKeep);
        CHECK(keep.kind == UiItemKind::Dropdown && keep.enabled && keep.text == L"Forever");
        CHECK(keep.options.size() == 5 && keep.options[2].text == L"1 year" && keep.options[2].value == 365 &&
              keep.options[4].text == L"Custom");
        CHECK(!Shown(frame, kRetention) && TextCount(frame, L"Custom retention") == 0);
        CHECK(HasText(frame, L"keep history forever"));
        // A preset; days no preset has, or Custom chosen: Custom with its box. Kept forever, Custom
        // chosen earlier shows none.
        struct RetentionCase {
            int days;
            bool custom;
            PCWSTR shown;
            PCWSTR box;  // The custom box's text; none: no box
        };
        for (const RetentionCase& c : {RetentionCase{365, false, L"1 year", nullptr}, RetentionCase{120, false, L"Custom", L"120"},
                                       RetentionCase{90, true, L"Custom", L"90"}, RetentionCase{3650, true, L"Custom", L"3,650"},
                                       RetentionCase{0, true, L"Forever", nullptr}}) {
            view.settings.historyRetentionDays = c.days;
            view.retentionCustom = c.custom;
            frame = {};
            SettingsUiBuildFrame(g, view, {1000, 820}, frame);
            CHECK(Control(frame, kKeep).text == c.shown && Shown(frame, kRetention) == bool(c.box) &&
                  TextCount(frame, L"Custom retention") == bool(c.box));
            if (c.box) {
                const UiItem& box = Control(frame, kRetention);
                CHECK(box.kind == UiItemKind::NumberBox && box.enabled && box.text == c.box &&
                      Control(frame, kRetention, 3).runs[0].text == L"days");
                CHECK(Control(frame, kKeep).rect.bottom < box.rect.top &&
                      box.rect.bottom < Control(frame, kRetention, 8).rect.top);
            }
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
        // Recording off: no retention cards, the note about accounts right under the switch.
        CHECK(!Shown(frame, kKeep) && !Shown(frame, kRetention) && TextCount(frame, L"Keep history") == 0 &&
              TextCount(frame, L"Custom retention") == 0);
        CHECK(Control(frame, SettingsField::RecordHistory).rect.bottom < Control(frame, kRetention, 8).rect.top);
        // Typed days that couldn't be saved keep their box, with the error.
        view.drafts.push_back({0, kRetention, 1, true, L"9x", 1500, false, false, SettingsDraftError::Range});
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(!Shown(frame, kKeep) && Control(frame, kRetention).enabled && Control(frame, kRetention).text == L"9x" &&
              HasText(frame, L"Enter a whole number from 1 to 3650."));
        view.drafts.clear();
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
