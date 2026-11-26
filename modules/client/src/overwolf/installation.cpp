#include "process.hpp"
#include <owh/platform/result.hpp>
#include <filesystem>
#include <vector>

namespace overwolf_hook::client::host {
void copy_utf8(char *output, size_t capacity, const std::wstring &text) {
    if (text.empty()) {
        output[0] = 0;
        return;
    }
    const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.c_str(), -1,
                                           nullptr, 0, nullptr, nullptr);
    if (!count)
        fail_win32(OWH_SYSTEM_ERROR, GetLastError(), OWH_STAGE_TARGET_DISCOVERY);
    if (static_cast<size_t>(count) > capacity)
        fail(OWH_BUFFER_TOO_SMALL, OWH_STAGE_TARGET_DISCOVERY);
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.c_str(), -1, output, count,
                             nullptr, nullptr))
        fail_win32(OWH_SYSTEM_ERROR, GetLastError(), OWH_STAGE_TARGET_DISCOVERY);
}

std::wstring file_version(const std::wstring &path) {
    DWORD unused = 0;
    const auto bytes = GetFileVersionInfoSizeW(path.c_str(), &unused);
    if (!bytes)
        return {};
    std::vector<std::byte> buffer(bytes);
    if (!GetFileVersionInfoW(path.c_str(), 0, bytes, buffer.data()))
        fail_win32(OWH_SYSTEM_ERROR, GetLastError(), OWH_STAGE_TARGET_DISCOVERY);
    VS_FIXEDFILEINFO *version = nullptr;
    UINT size = 0;
    if (!VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<void **>(&version), &size) ||
        size < sizeof(*version) || version->dwSignature != 0xfeef04bd)
        return {};
    return std::to_wstring(HIWORD(version->dwProductVersionMS)) + L"." +
           std::to_wstring(LOWORD(version->dwProductVersionMS)) + L"." +
           std::to_wstring(HIWORD(version->dwProductVersionLS)) + L"." +
           std::to_wstring(LOWORD(version->dwProductVersionLS));
}

namespace {
std::wstring registry_string(HKEY key, const wchar_t *value) {
    DWORD bytes = 0;
    auto status = RegGetValueW(key, nullptr, value, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
                               nullptr, nullptr, &bytes);
    if (status == ERROR_FILE_NOT_FOUND)
        return {};
    if (status != ERROR_SUCCESS)
        fail_win32(OWH_SYSTEM_ERROR, status, OWH_STAGE_TARGET_DISCOVERY);
    std::vector<wchar_t> text(bytes / sizeof(wchar_t) + 1);
    status = RegGetValueW(key, nullptr, value, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
                          nullptr, text.data(), &bytes);
    if (status != ERROR_SUCCESS)
        fail_win32(OWH_SYSTEM_ERROR, status, OWH_STAGE_TARGET_DISCOVERY);
    return text.data();
}
struct RegistryKey {
    HKEY value = nullptr;
    ~RegistryKey() { if (value) RegCloseKey(value); }
};
} // namespace

owh_result installation(owh_overwolf_installation &output) noexcept {
    return boundary([&] {
        constexpr auto uninstall = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Overwolf";
        LSTATUS denied = ERROR_SUCCESS;
        for (auto hive : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
            for (auto view : {KEY_WOW64_32KEY, KEY_WOW64_64KEY}) {
                RegistryKey key;
                const auto status = RegOpenKeyExW(hive, uninstall, 0, KEY_QUERY_VALUE | view,
                                                  &key.value);
                if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND)
                    continue;
                if (status != ERROR_SUCCESS) {
                    denied = status;
                    continue;
                }
                const auto directory = registry_string(key.value, L"InstallLocation");
                if (directory.empty())
                    continue;
                const auto base = std::filesystem::path(directory);
                auto launcher = base / L"OverwolfLauncher.exe";
                if (!std::filesystem::is_regular_file(launcher))
                    launcher = base / L"Overwolf.exe";
                if (!std::filesystem::is_regular_file(launcher))
                    continue;
                owh_overwolf_installation info{};
                info.size = sizeof(info);
                copy_utf8(info.directory, sizeof(info.directory), base.wstring());
                copy_utf8(info.launcher, sizeof(info.launcher), launcher.wstring());
                // Registry DisplayVersion describes the installed product. A
                // running process is instead described from its actual image.
                auto version = registry_string(key.value, L"DisplayVersion");
                if (version.empty())
                    version = file_version((base / L"Overwolf.exe").wstring());
                copy_utf8(info.version, sizeof(info.version), version);
                output = info;
                return make_result(OWH_OK);
            }
        }
        return denied != ERROR_SUCCESS
                   ? make_result(OWH_SYSTEM_ERROR, OWH_STAGE_TARGET_DISCOVERY, OWH_ERROR_WIN32, denied)
                   : make_result(OWH_NOT_FOUND, OWH_STAGE_TARGET_DISCOVERY);
    });
}
} // namespace overwolf_hook::client::host
