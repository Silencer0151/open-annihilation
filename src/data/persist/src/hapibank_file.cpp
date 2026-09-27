// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/persist/hapibank.hpp"
#include "oa/data/persist/squash.hpp"

#include "bank_util.hpp"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace oa::data::persist {
namespace {

using detail::load_le32;
using detail::store_le32;

constexpr uint32_t max_image_bytes = 1u << 30;
constexpr uint32_t packed_flag = 1;
constexpr int32_t blob_id_marker = -1; // name slot of an id-addressed blob entry
constexpr char audit_extension[] = ".cpa";

bool reserve(ByteImage* image, uint32_t bytes) {
    if (bytes <= image->capacity)
        return true;
    if (bytes > max_image_bytes)
        return false;
    uint32_t capacity = image->capacity < 256 ? 256 : image->capacity;
    while (capacity < bytes)
        capacity = capacity > max_image_bytes / 2 ? max_image_bytes : capacity * 2;
    auto* grown = static_cast<uint8_t*>(std::realloc(image->data, capacity));
    if (grown == nullptr)
        return false;
    image->data = grown;
    image->capacity = capacity;
    return true;
}

// Positioned writes over the image of a file.
struct Stream {
    ByteImage* image;
    uint32_t position;
    bool failed;
};

void stream_write(Stream& s, const void* data, uint32_t bytes) {
    if (s.failed)
        return;
    const uint64_t end = static_cast<uint64_t>(s.position) + bytes;
    if (end > max_image_bytes || !reserve(s.image, static_cast<uint32_t>(end))) {
        s.failed = true;
        return;
    }
    if (s.position > s.image->size)
        std::memset(s.image->data + s.image->size, 0, s.position - s.image->size);
    if (bytes != 0)
        std::memcpy(s.image->data + s.position, data, bytes);
    s.position = static_cast<uint32_t>(end);
    if (s.position > s.image->size)
        s.image->size = s.position;
}

void stream_write_u32(Stream& s, uint32_t value) {
    uint8_t bytes[4];
    store_le32(bytes, value);
    stream_write(s, bytes, 4);
}

void set_error(BankError* error, const char* format, const char* detail) {
    if (error != nullptr)
        std::snprintf(
            error->message, sizeof(error->message), format, detail != nullptr ? detail : ""
        );
}

// A NUL-terminated string inside the pool, or nullptr.
const char* pool_string(const ByteImage& pool, uint32_t offset) {
    if (offset >= pool.size)
        return nullptr;
    const void* end = std::memchr(pool.data + offset, '\0', pool.size - offset);
    return end != nullptr ? reinterpret_cast<const char*>(pool.data + offset) : nullptr;
}

bool appendf(ByteImage* out, const char* format, ...) {
    char line[512];
    va_list args;
    va_start(args, format);
    const int length = std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (length < 0)
        return false;
    const auto count =
        static_cast<uint32_t>(length < static_cast<int>(sizeof(line)) ? length : sizeof(line) - 1);
    Stream s{out, out->size, false};
    for (uint32_t i = 0; i < count; ++i) {
        if (line[i] == '\n')
            stream_write(s, "\r", 1);
        stream_write(s, &line[i], 1);
    }
    return !s.failed;
}

/// Parses one account record of a HAPIBANK image into the bank.
///
/// The payload is unpacked when the record is marked packed; the account is
/// then opened (created) and its integer, double, string and blob entries
/// stored. A record for another account than `only_account` is skipped.
///
/// @param[in,out] bank bank that receives the account
/// @param image file image
/// @param size end of the account area (the pool offset)
/// @param[in,out] position offset of the record; advanced past it on success
/// @param pool unpacked string pool
/// @param only_account when not null, the only account read
/// @param[out] error receives a message on failure; may be null
/// @return false when the record is truncated, out of range, fails to unpack
///     or has a malformed entry table
bool read_account(
    Bank* bank,
    const uint8_t* image,
    uint32_t size,
    uint32_t& position,
    const ByteImage& pool,
    const char* only_account,
    BankError* error
) {
    const uint32_t start = position;
    if (size - start < account_header_bytes) {
        set_error(error, "bank account header is cut short%s", nullptr);
        return false;
    }
    const uint8_t* header = image + start;
    const uint32_t record_bytes = load_le32(header + account_header::size);
    const char* name = pool_string(pool, load_le32(header + account_header::name));
    if (name == nullptr || record_bytes > size - start) {
        set_error(error, "bank account record runs past the end of the file%s", nullptr);
        return false;
    }
    if (only_account != nullptr && !detail::equal_nocase(only_account, name)) {
        if (record_bytes <= account_header_bytes) {
            set_error(error, "bank account record has no size%s", nullptr);
            return false;
        }
        position = start + record_bytes;
        return true;
    }
    position = start + account_header_bytes;
    if (record_bytes <= account_header_bytes)
        return true;
    const uint32_t stored = record_bytes - account_header_bytes;
    const uint8_t* stored_bytes = image + start + account_header_bytes;
    uint8_t* payload = nullptr;
    uint32_t payload_bytes = 0;
    if (load_le32(header + account_header::packed) == packed_flag) {
        const uint32_t unpacked = squash_unpacked_size(stored_bytes, stored);
        auto* block = static_cast<uint8_t*>(std::malloc(stored));
        payload = unpacked <= max_image_bytes ? static_cast<uint8_t*>(std::malloc(unpacked + 1u))
                                              : nullptr;
        SquashStatus status = SquashStatus::bad_params;
        if (block != nullptr && payload != nullptr) {
            std::memcpy(block, stored_bytes, stored);
            status = squash_unpack(payload, unpacked, block, stored);
        }
        std::free(block);
        if (status != SquashStatus::ok) {
            std::free(payload);
            set_error(error, "bank account data failed to unpack: %s", squash_status_name(status));
            return false;
        }
        payload_bytes = unpacked;
    } else {
        payload = static_cast<uint8_t*>(std::malloc(stored));
        if (payload == nullptr) {
            set_error(error, "not enough memory for a bank account%s", nullptr);
            return false;
        }
        std::memcpy(payload, stored_bytes, stored);
        payload_bytes = stored;
    }

    bank_open_account(bank, name);
    const auto int_count = static_cast<int32_t>(load_le32(header + account_header::int_count));
    const auto real_count = static_cast<int32_t>(load_le32(header + account_header::real_count));
    const auto text_count = static_cast<int32_t>(load_le32(header + account_header::text_count));
    const auto blob_count = static_cast<int32_t>(load_le32(header + account_header::blob_count));
    const uint64_t table_bytes =
        static_cast<uint64_t>(int_count > 0 ? int_count : 0) * int_entry_bytes +
        static_cast<uint64_t>(real_count > 0 ? real_count : 0) * real_entry_bytes +
        static_cast<uint64_t>(text_count > 0 ? text_count : 0) * text_entry_bytes +
        static_cast<uint64_t>(blob_count > 0 ? blob_count : 0) * blob_entry_bytes;
    bool ok = table_bytes <= payload_bytes;
    const uint8_t* entry = payload;
    for (int32_t i = 0; ok && i < int_count; ++i, entry += int_entry_bytes) {
        const char* field = pool_string(pool, load_le32(entry));
        ok = field != nullptr &&
             bank_set_int(bank, field, static_cast<int32_t>(load_le32(entry + 4)));
    }
    for (int32_t i = 0; ok && i < real_count; ++i, entry += real_entry_bytes) {
        const char* field = pool_string(pool, load_le32(entry));
        double value = 0;
        const uint64_t bits =
            load_le32(entry + 4) | (static_cast<uint64_t>(load_le32(entry + 8)) << 32);
        std::memcpy(&value, &bits, sizeof(value));
        ok = field != nullptr && bank_set_real(bank, field, value);
    }
    for (int32_t i = 0; ok && i < text_count; ++i, entry += text_entry_bytes) {
        const char* field = pool_string(pool, load_le32(entry));
        const char* value = pool_string(pool, load_le32(entry + 4));
        ok = field != nullptr && value != nullptr && bank_set_text(bank, field, value);
    }
    for (int32_t i = 0; ok && i < blob_count; ++i, entry += blob_entry_bytes) {
        const auto name_offset = static_cast<int32_t>(load_le32(entry));
        const uint32_t data_offset = load_le32(entry + 8);
        const uint32_t data_bytes = load_le32(entry + 12);
        if (name_offset < 0) {
            bank_open_blob_id(bank, static_cast<int32_t>(load_le32(entry + 4)));
        } else {
            const char* blob = pool_string(pool, static_cast<uint32_t>(name_offset));
            ok = blob != nullptr;
            if (ok)
                bank_open_blob_name(bank, blob);
        }
        // Blob offsets are file positions of the unpacked record.
        const uint64_t relative = static_cast<uint64_t>(data_offset) - start - account_header_bytes;
        ok = ok && data_offset >= start + account_header_bytes &&
             relative + data_bytes <= payload_bytes &&
             bank_blob_write(bank, payload + relative, data_bytes) == data_bytes;
        bank_blob_seek(bank, 0);
    }
    std::free(payload);
    if (!ok) {
        set_error(error, "bank account \"%s\" has a malformed entry table", name);
        return false;
    }
    position = start + record_bytes;
    return true;
}

} // namespace

void byte_image_free(ByteImage* image) {
    std::free(image->data);
    *image = ByteImage{};
}

bool byte_image_assign(ByteImage* image, const uint8_t* data, uint32_t size) {
    image->size = 0;
    if (!reserve(image, size))
        return false;
    if (size != 0)
        std::memcpy(image->data, data, size);
    image->size = size;
    return true;
}

int32_t bank_pool_append(ByteImage* pool, const char* text) {
    const auto offset = static_cast<int32_t>(pool->size);
    Stream s{pool, pool->size, false};
    stream_write(s, text, static_cast<uint32_t>(std::strlen(text) + 1));
    return s.failed ? -1 : offset;
}

bool bank_write_account(
    const Bank* bank, int32_t index, ByteImage* file, ByteImage* pool, bool pack
) {
    const BankAccount& account = bank->accounts->items[index];
    if (account.field_count <= 0 && account.blob_count <= 0)
        return true;
    const uint32_t start = file->size;
    Stream s{file, start, false};
    uint8_t header[account_header_bytes] = {};
    stream_write(s, header, account_header_bytes);
    store_le32(
        header + account_header::name, static_cast<uint32_t>(bank_pool_append(pool, account.name))
    );

    uint32_t ints = 0, reals = 0, texts = 0, blobs = 0;
    for (int32_t i = 0; i < account.field_count; ++i) {
        const BankField& field = account.fields[i];
        if (field.type != BankFieldType::integer)
            continue;
        stream_write_u32(s, static_cast<uint32_t>(bank_pool_append(pool, field.name)));
        stream_write_u32(s, static_cast<uint32_t>(field.integer));
        ++ints;
    }
    // The id slot of a named blob entry repeats the last value written here:
    // the previous double's name offset or id-blob id, or zero before either.
    uint32_t carried = 0;
    for (int32_t i = 0; i < account.field_count; ++i) {
        const BankField& field = account.fields[i];
        if (field.type != BankFieldType::real)
            continue;
        carried = static_cast<uint32_t>(bank_pool_append(pool, field.name));
        uint64_t bits = 0;
        std::memcpy(&bits, &field.real, sizeof(bits));
        stream_write_u32(s, carried);
        stream_write_u32(s, static_cast<uint32_t>(bits));
        stream_write_u32(s, static_cast<uint32_t>(bits >> 32));
        ++reals;
    }
    for (int32_t i = 0; i < account.field_count; ++i) {
        const BankField& field = account.fields[i];
        if (field.type != BankFieldType::text)
            continue;
        const auto name = static_cast<uint32_t>(bank_pool_append(pool, field.name));
        const auto value = static_cast<uint32_t>(bank_pool_append(pool, field.text));
        stream_write_u32(s, name);
        stream_write_u32(s, value);
        ++texts;
    }
    for (int32_t i = 0; i < account.blob_count; ++i)
        if (account.blobs[i].size > 0)
            ++blobs;
    uint32_t data_offset = s.position + blobs * blob_entry_bytes;
    for (int32_t i = 0; i < account.blob_count; ++i) {
        const BankBlob& blob = account.blobs[i];
        if (blob.size <= 0)
            continue;
        uint32_t name = 0;
        if (blob.named == 0) {
            name = static_cast<uint32_t>(blob_id_marker);
            carried = static_cast<uint32_t>(blob.id);
        } else {
            name = static_cast<uint32_t>(bank_pool_append(pool, blob.name));
        }
        stream_write_u32(s, name);
        stream_write_u32(s, carried);
        stream_write_u32(s, data_offset);
        stream_write_u32(s, static_cast<uint32_t>(blob.size));
        data_offset += static_cast<uint32_t>(blob.size);
    }
    for (int32_t i = 0; i < account.blob_count; ++i)
        if (account.blobs[i].size > 0)
            stream_write(s, account.blobs[i].data, static_cast<uint32_t>(account.blobs[i].size));
    if (s.failed)
        return false;

    uint32_t record_bytes = s.position - start;
    if (pack) {
        const uint32_t raw = record_bytes - account_header_bytes;
        uint32_t capacity = squash_bound(static_cast<int32_t>(raw), SquashType::lz77);
        auto* packed = static_cast<uint8_t*>(std::malloc(capacity != 0 ? capacity : 1));
        if (packed != nullptr &&
            squash_pack(
                packed,
                &capacity,
                file->data + start + account_header_bytes,
                raw,
                SquashType::lz77,
                false
            ) == SquashStatus::ok &&
            static_cast<int32_t>(capacity) < static_cast<int32_t>(raw)) {
            s.position = start + account_header_bytes;
            stream_write(s, packed, capacity);
            record_bytes = capacity + account_header_bytes;
            store_le32(header + account_header::packed, packed_flag);
            file->size = s.position;
        }
        std::free(packed);
    }
    store_le32(header + account_header::size, record_bytes);
    store_le32(header + account_header::int_count, ints);
    store_le32(header + account_header::real_count, reals);
    store_le32(header + account_header::text_count, texts);
    store_le32(header + account_header::blob_count, blobs);
    std::memcpy(file->data + start, header, account_header_bytes);
    return !s.failed;
}

bool bank_write_image(
    const Bank* bank, const char* description, bool pack_accounts, ByteImage* out
) {
    if (bank == nullptr || bank->accounts == nullptr || bank->accounts->count == 0)
        return false;
    out->size = 0;
    ByteImage pool{};
    uint8_t header[bank_header_bytes] = {};
    std::memcpy(header + bank_header::magic, bank_magic, sizeof(bank_magic));
    store_le32(header + bank_header::version, bank_format_version);
    store_le32(
        header + bank_header::description,
        static_cast<uint32_t>(bank_pool_append(&pool, description != nullptr ? description : ""))
    );
    store_le32(header + bank_header::first_account, bank_header_bytes);
    Stream s{out, 0, false};
    stream_write(s, header, bank_header_bytes);
    bool ok = !s.failed;
    for (int32_t i = 0; ok && i < bank->accounts->count; ++i)
        ok = bank_write_account(bank, i, out, &pool, pack_accounts);
    if (!ok) {
        byte_image_free(&pool);
        return false;
    }
    const uint32_t pool_offset = out->size;
    s.position = pool_offset;
    // The pool budget uses the zlib percentage although LZ77 packs it.
    uint32_t capacity = squash_bound(static_cast<int32_t>(pool.size), SquashType::zlib);
    auto* packed = static_cast<uint8_t*>(std::malloc(capacity != 0 ? capacity : 1));
    if (packed == nullptr ||
        squash_pack(packed, &capacity, pool.data, pool.size, SquashType::lz77, false) !=
            SquashStatus::ok ||
        static_cast<int32_t>(pool.size) <= static_cast<int32_t>(capacity)) {
        stream_write(s, pool.data, pool.size);
    } else {
        stream_write(s, packed, capacity);
        header[bank_header::pool_packed] = 1;
    }
    std::free(packed);
    byte_image_free(&pool);
    store_le32(header + bank_header::pool_offset, pool_offset);
    if (s.failed)
        return false;
    std::memcpy(out->data, header, bank_header_bytes);
    return true;
}

bool bank_format_audit(const Bank* bank, ByteImage* out) {
    out->size = 0;
    if (bank == nullptr || bank->accounts == nullptr)
        return false;
    const BankAccounts& accounts = *bank->accounts;
    bool ok = appendf(out, "Saved-game bank contents\n\n");
    ok = ok && appendf(out, "Accounts in this bank: %i\n\n", accounts.count);
    for (int32_t a = 0; ok && a < accounts.count; ++a) {
        const BankAccount& account = accounts.items[a];
        ok = appendf(out, "Bank account \"%s\"\n{\n", account.name);
        for (int32_t i = 0; ok && i < account.field_count; ++i) {
            const BankField& field = account.fields[i];
            switch (field.type) {
            case BankFieldType::integer:
                ok = appendf(
                    out,
                    "   \"%s\" is a whole number: %i (0x%08x)\n",
                    field.name,
                    field.integer,
                    static_cast<unsigned>(field.integer)
                );
                break;
            case BankFieldType::real:
                ok = appendf(out, "   \"%s\" is a real number: %f\n", field.name, field.real);
                break;
            case BankFieldType::text:
                ok = appendf(out, "   \"%s\" is text: \"%s\"\n", field.name, field.text);
                break;
            default:
                ok = appendf(out, "   \"%s\" has no value or an unknown type\n", field.name);
                break;
            }
        }
        ok = ok && appendf(out, "\n");
        for (int32_t i = 0; ok && i < account.blob_count; ++i) {
            const BankBlob& blob = account.blobs[i];
            ok = blob.named == 0
                     ? appendf(out, "   Data block #%i holds %i bytes\n", blob.id, blob.size)
                     : appendf(out, "   Data block \"%s\" holds %i bytes\n", blob.name, blob.size);
        }
        ok = ok && appendf(out, "}\n\n");
    }
    return ok && appendf(out, "(end of listing)\n");
}

bool bank_read_image(
    Bank* bank,
    const uint8_t* image,
    uint32_t size,
    const char* description,
    const char* only_account,
    BankError* error
) {
    if (error != nullptr)
        error->message[0] = '\0';
    if (image == nullptr || size < bank_header_bytes ||
        std::memcmp(image + bank_header::magic, bank_magic, sizeof(bank_magic)) != 0 ||
        load_le32(image + bank_header::version) != bank_format_version) {
        set_error(error, "not a saved-game bank%s", nullptr);
        return false;
    }
    const uint32_t pool_offset = load_le32(image + bank_header::pool_offset);
    uint32_t position = load_le32(image + bank_header::first_account);
    if (pool_offset > size || position < bank_header_bytes || position > pool_offset) {
        set_error(error, "bank layout runs past the end of the file%s", nullptr);
        return false;
    }
    ByteImage pool{};
    const uint32_t stored = size - pool_offset;
    if (image[bank_header::pool_packed] == 0) {
        if (!byte_image_assign(&pool, image + pool_offset, stored)) {
            set_error(error, "not enough memory for the bank's names%s", nullptr);
            return false;
        }
    } else {
        const uint32_t unpacked = squash_unpacked_size(image + pool_offset, stored);
        ByteImage block{};
        SquashStatus status = SquashStatus::bad_params;
        if (unpacked <= max_image_bytes && byte_image_assign(&block, image + pool_offset, stored) &&
            reserve(&pool, unpacked + 1u)) {
            status = squash_unpack(pool.data, unpacked, block.data, stored);
            pool.size = unpacked;
        }
        byte_image_free(&block);
        if (status != SquashStatus::ok) {
            byte_image_free(&pool);
            set_error(error, "the bank's names failed to unpack: %s", squash_status_name(status));
            return false;
        }
    }
    const char* stored_description = pool_string(pool, load_le32(image + bank_header::description));
    if (stored_description == nullptr ||
        (description != nullptr && !detail::equal_nocase(stored_description, description))) {
        byte_image_free(&pool);
        set_error(error, "the bank's description does not match%s", nullptr);
        return false;
    }
    bool ok = bank_reset(bank);
    while (ok && position < pool_offset)
        ok = read_account(bank, image, pool_offset, position, pool, only_account, error);
    byte_image_free(&pool);
    return ok;
}

FileSink stdio_file_sink() {
    return FileSink{nullptr, [](void*, const char* path, const uint8_t* data, std::size_t size) {
                        std::FILE* file = std::fopen(path, "wb");
                        if (file == nullptr)
                            return false;
                        const bool written = size == 0 || std::fwrite(data, 1, size, file) == size;
                        return std::fclose(file) == 0 && written;
                    }};
}

FileSource stdio_file_source() {
    return FileSource{nullptr, [](void*, const char* path, ByteImage* out) {
                          std::FILE* file = std::fopen(path, "rb");
                          if (file == nullptr)
                              return false;
                          bool ok = std::fseek(file, 0, SEEK_END) == 0;
                          const long length = ok ? std::ftell(file) : -1;
                          ok = ok && length >= 0 &&
                               static_cast<unsigned long>(length) <= max_image_bytes &&
                               std::fseek(file, 0, SEEK_SET) == 0 &&
                               reserve(out, static_cast<uint32_t>(length));
                          if (ok) {
                              out->size = static_cast<uint32_t>(length);
                              ok = length == 0 ||
                                   std::fread(out->data, 1, out->size, file) == out->size;
                          }
                          std::fclose(file);
                          return ok;
                      }};
}

bool bank_write_file(
    const Bank* bank,
    const char* path,
    const char* description,
    bool pack_accounts,
    bool audit,
    const FileSink* files
) {
    if (bank == nullptr || bank->accounts == nullptr || bank->accounts->count == 0 ||
        path == nullptr || files == nullptr)
        return false;
    if (audit) {
        char audit_path[260];
        std::snprintf(audit_path, sizeof(audit_path), "%s", path);
        char* dot = std::strrchr(audit_path, '.');
        const char* slash = std::strpbrk(dot != nullptr ? dot : audit_path, "\\/");
        if (dot != nullptr && slash == nullptr)
            *dot = '\0';
        if (std::strlen(audit_path) + sizeof(audit_extension) <= sizeof(audit_path))
            std::strcat(audit_path, audit_extension);
        ByteImage text{};
        if (bank_format_audit(bank, &text))
            files->write_file(files->context, audit_path, text.data, text.size);
        byte_image_free(&text);
    }
    ByteImage image{};
    const bool ok = bank_write_image(bank, description, pack_accounts, &image) &&
                    files->write_file(files->context, path, image.data, image.size);
    byte_image_free(&image);
    return ok;
}

bool bank_read_file(
    Bank* bank,
    const char* path,
    const char* description,
    const char* only_account,
    const FileSource* files,
    BankError* error
) {
    ByteImage image{};
    if (path == nullptr || files == nullptr || !files->read_file(files->context, path, &image)) {
        byte_image_free(&image);
        set_error(error, "cannot read %s", path);
        return false;
    }
    const bool ok = bank_read_image(bank, image.data, image.size, description, only_account, error);
    byte_image_free(&image);
    return ok;
}

} // namespace oa::data::persist
