#include <windows.h>
#include <cwchar>

int wmain(int argc, wchar_t **argv) {
    if (argc != 4) return 2;
    const bool missing = std::wcscmp(argv[3], L"missing") == 0;
    const bool ignore = std::wcscmp(argv[3], L"ignore") == 0;
    if (!missing && !ignore && std::wcscmp(argv[3], L"exit") != 0) return 3;

    // Deliberately windowless: the cooperative application event is sufficient.
    HANDLE shutdown = missing ? nullptr : CreateEventW(nullptr, FALSE, FALSE, argv[2]);
    if (!missing && !shutdown) return 4;
    HANDLE wait = missing || ignore ? CreateEventW(nullptr, FALSE, FALSE, nullptr) : shutdown;
    if (!wait) return 5;
    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[1]);
    if (!ready) return 6;
    const auto announced = SetEvent(ready);
    CloseHandle(ready);
    if (!announced) return 7;

    const auto result = WaitForSingleObject(wait, INFINITE);
    CloseHandle(wait);
    if (shutdown && shutdown != wait) CloseHandle(shutdown);
    return result == WAIT_OBJECT_0 ? 0 : 8;
}
