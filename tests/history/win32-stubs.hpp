// Standalone prelude: Windows headers, the standard library and the globals the extracted
// sections read. No windows, settings, journal storage, auth or network.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

static std::atomic<bool> g_unloading{false};
// The journal's status notifier posts to the settings and history windows; here it only counts.
static int settingsNotifications = 0;
static void NotifySettingsWindowChanged(bool = false) { ++settingsNotifications; }
