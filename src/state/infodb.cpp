#include "infodb.hpp"
#include "transport/channel.hpp"
#include <cstring>
#include <set>

namespace overwolf_hook {
namespace {
class Cursor {
public:
    explicit Cursor(std::span<const std::byte> bytes) : bytes_(bytes) {}
    template<typename T> T integer() {
        const auto value = take(sizeof(T));
        T result;
        std::memcpy(&result, value.data(), sizeof(result));
        return result;
    }
    std::span<const std::byte> take(size_t count) {
        if (count > bytes_.size() - offset_) fail(OWH_CORRUPT);
        const auto result = bytes_.subspan(offset_, count);
        offset_ += count;
        return result;
    }
    std::string name() {
        const size_t start = offset_;
        for (size_t n = 0; n <= 256 && offset_ < bytes_.size(); ++n) {
            if (integer<uint8_t>() == 0) {
                if (!n) fail(OWH_CORRUPT);
                return std::string(reinterpret_cast<const char*>(bytes_.data() + start), n);
            }
        }
        fail(OWH_CORRUPT);
    }
    bool finished() const { return offset_ == bytes_.size(); }
private:
    std::span<const std::byte> bytes_;
    size_t offset_ = 0;
};
}

std::vector<InfoField> decode_infodb(std::span<const std::byte> bytes) {
    if (bytes.size() > maximum_infodb_bytes) fail(OWH_CORRUPT);
    Cursor header(bytes);
    const uint32_t metadata_end = header.integer<uint32_t>();
    if (metadata_end < sizeof(uint32_t) || metadata_end > bytes.size()) fail(OWH_CORRUPT);
    Cursor metadata(bytes.subspan(sizeof(uint32_t), metadata_end - sizeof(uint32_t)));
    std::vector<InfoField> fields;
    std::set<std::pair<std::string, std::string>> identities;
    size_t payload_bytes = 0;
    while (!metadata.finished()) {
        const auto category = metadata.name();
        const auto count = metadata.integer<uint32_t>();
        if (count > 4096 - fields.size()) fail(OWH_CORRUPT);
        for (uint32_t i = 0; i < count; ++i) {
            const auto key = metadata.name();
            const auto capacity = metadata.integer<uint32_t>();
            if (capacity > maximum_infodb_bytes ||
                !identities.emplace(category, key).second) fail(OWH_CORRUPT);
            payload_bytes += sizeof(uint32_t) + capacity;
            if (payload_bytes > maximum_infodb_bytes) fail(OWH_CORRUPT);
            fields.push_back({category, key, 0, capacity, {}});
        }
    }
    Cursor payload(bytes.subspan(metadata_end));
    for (auto& field : fields) field.ticks = payload.integer<uint64_t>();
    for (auto& field : fields) {
        const auto size = payload.integer<uint32_t>();
        if (size > field.capacity) fail(OWH_CORRUPT);
        const auto reserved = payload.take(field.capacity);
        field.bytes.assign(reserved.begin(), reserved.begin() + size);
    }
    return fields;
}
}
