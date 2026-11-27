#include <windows.h>
#include <bcrypt.h>
#include <owh/platform/handle.hpp>
#include <owh/platform/sha256.hpp>
#include <vector>

namespace overwolf_hook::platform {
bool sha256_file(const std::wstring &path, std::array<uint8_t, 32> &digest) noexcept {
    Handle file(CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!file)
        return false;
    try {
        struct HashOwner {
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            BCRYPT_HASH_HANDLE hash = nullptr;
            std::vector<uint8_t> object;
            ~HashOwner() {
                // CNG borrows object until the hash is destroyed.
                if (hash)
                    BCryptDestroyHash(hash);
                if (algorithm)
                    BCryptCloseAlgorithmProvider(algorithm, 0);
            }
        } state;
        if (BCryptOpenAlgorithmProvider(&state.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            return false;
        DWORD object_bytes = 0, returned = 0;
        if (BCryptGetProperty(state.algorithm, BCRYPT_OBJECT_LENGTH,
                              reinterpret_cast<PUCHAR>(&object_bytes), sizeof(object_bytes),
                              &returned, 0) < 0 ||
            !object_bytes)
            return false;
        state.object.resize(object_bytes);
        if (BCryptCreateHash(state.algorithm, &state.hash, state.object.data(), object_bytes,
                             nullptr, 0, 0) < 0)
            return false;
        std::array<uint8_t, 16384> chunk{};
        for (;;) {
            DWORD bytes = 0;
            if (!ReadFile(file.get(), chunk.data(), static_cast<DWORD>(chunk.size()), &bytes,
                          nullptr))
                return false;
            if (!bytes)
                break;
            if (BCryptHashData(state.hash, chunk.data(), bytes, 0) < 0)
                return false;
        }
        return BCryptFinishHash(state.hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >=
               0;
    } catch (...) {
        return false;
    }
}
} // namespace overwolf_hook::platform
