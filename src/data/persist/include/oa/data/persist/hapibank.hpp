// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// HAPIBANK: the savegame container. A bank holds named accounts; an account
// holds typed fields (integer, double, string) and byte blobs addressed by id
// or name. Field and blob calls act on the currently open account, and blob
// byte I/O on that account's open blob, as in the game.
#pragma once

#include <cstddef>
#include <cstdint>

namespace oa::data::persist {

enum class BankFieldType : int32_t { unset = 0, integer = 1, real = 2, text = 3 };

struct BankField {
    char* name;
    BankFieldType type;
    int32_t integer;
    double real;
    char* text; // owned when type == text
};

struct BankBlob {
    int32_t named; // 0: addressed by id, 1: by name
    int32_t id;
    char* name;
    int32_t size;
    int32_t position;
    uint8_t* data;
};

struct BankAccount {
    char* name;
    int32_t field_count;
    int32_t blob_count;
    int32_t open_blob; // -1 none
    BankField* fields;
    BankBlob* blobs;
};

struct BankAccounts {
    int32_t count;
    BankAccount* items;
    int32_t open; // -1 none
};

// A bank: its account table.
struct Bank {
    BankAccounts* accounts;
};

// Growable byte buffer used for bank images, audit text and file contents.
struct ByteImage {
    uint8_t* data;
    uint32_t size;
    uint32_t capacity;
};

/// Frees a byte buffer and empties it.
///
/// @param[in,out] image buffer to release
void byte_image_free(ByteImage* image);

/// Replaces a byte buffer's contents, reusing its storage when it is large enough.
///
/// @param[in,out] image buffer to fill; its size is 0 when the call fails
/// @param data bytes to copy; may be null when `size` is 0
/// @param size number of bytes; at most 1 GiB
/// @return false when the buffer cannot grow
bool byte_image_assign(ByteImage* image, const uint8_t* data, uint32_t size);

/// Clears the account-table pointer without releasing it.
///
/// @param[out] bank bank to reset
void bank_init(Bank* bank);

/// Releases every account, field, string and blob, and the account table.
///
/// @param[in,out] bank bank to release; left with no account table
void bank_destroy(Bank* bank);

/// Releases the bank and starts an empty account table with no open account.
///
/// @param[in,out] bank bank to reset
/// @return false when the table cannot be allocated (the bank is left without one)
bool bank_reset(Bank* bank);

/// Opens an account by name, creating it when absent.
///
/// Opening an account closes its open blob.
///
/// @param[in,out] bank bank with an account table
/// @param name account name, matched case-insensitively; copied for a new account
/// @return true when the account already existed; false when it was created,
///     when there is no table or no name, or when the creation failed (which
///     leaves no account open)
bool bank_open_account(Bank* bank, const char* name);

/// Finds a field in the open account, optionally appending an unset one.
///
/// @param[in,out] bank bank with an open account
/// @param name field name, matched case-insensitively; copied for a new field
/// @param create true to append the field when it is absent
/// @return field index, or -1 when absent (and not created), when no account
///     is open or `name` is null
int32_t bank_find_field(Bank* bank, const char* name, bool create);

/// Tests whether the open account has a field, of any type.
///
/// @param[in,out] bank bank with an open account
/// @param name field name, matched case-insensitively
/// @return true when the field exists
bool bank_has_field(Bank* bank, const char* name);

/// Stores an integer field in the open account, replacing any value of any type.
///
/// @param[in,out] bank bank with an open account
/// @param name field name, matched case-insensitively
/// @param value value to store
/// @return false when no account is open or the field cannot be created
bool bank_set_int(Bank* bank, const char* name, int32_t value);

/// Stores a double field in the open account, replacing any value of any type.
///
/// @param[in,out] bank bank with an open account
/// @param name field name, matched case-insensitively
/// @param value value to store
/// @return false when no account is open or the field cannot be created
bool bank_set_real(Bank* bank, const char* name, double value);

/// Stores a copy of a string field in the open account, replacing any value of any type.
///
/// @param[in,out] bank bank with an open account
/// @param name field name, matched case-insensitively
/// @param value NUL-terminated text to copy
/// @return false when `value` is null, no account is open or an allocation fails
bool bank_set_text(Bank* bank, const char* name, const char* value);

/// Reads an integer field of the open account.
///
/// @param[in,out] bank bank with an open account
/// @param name field name, matched case-insensitively
/// @param fallback value returned when the field is missing or not an integer
/// @return the stored integer, or `fallback`
int32_t bank_get_int(Bank* bank, const char* name, int32_t fallback);

/// Reads a double field of the open account.
///
/// @param[in,out] bank bank with an open account
/// @param name field name, matched case-insensitively
/// @param fallback value returned when the field is missing or not a double
/// @return the stored double, or `fallback`
double bank_get_real(Bank* bank, const char* name, double fallback);

/// Reads a string field of the open account.
///
/// @param[in,out] bank bank with an open account
/// @param name field name, matched case-insensitively
/// @param fallback value returned when the field is missing or not a string
/// @return the stored text, owned by the bank, or `fallback`
const char* bank_get_text(Bank* bank, const char* name, const char* fallback);

/// Finds an id-addressed blob in the open account, optionally appending an empty one.
///
/// @param[in,out] bank bank with an open account
/// @param id blob id
/// @param create true to append the blob when it is absent
/// @return blob index, or -1 when absent (and not created) or no account is open
int32_t bank_find_blob_id(Bank* bank, int32_t id, bool create);

/// Finds a name-addressed blob in the open account, optionally appending an empty one.
///
/// @param[in,out] bank bank with an open account
/// @param name blob name, matched case-insensitively; copied for a new blob
/// @param create true to append the blob when it is absent
/// @return blob index, or -1 when absent (and not created), when no account is
///     open or `name` is null
int32_t bank_find_blob_name(Bank* bank, const char* name, bool create);

/// Makes an id-addressed blob of the open account current, creating it when absent.
///
/// The blob keeps its read/write position.
///
/// @param[in,out] bank bank with an open account
/// @param id blob id
/// @return true when the blob holds data
bool bank_open_blob_id(Bank* bank, int32_t id);

/// Makes a name-addressed blob of the open account current, creating it when absent.
///
/// The blob keeps its read/write position.
///
/// @param[in,out] bank bank with an open account
/// @param name blob name, matched case-insensitively
/// @return true when the blob holds data
bool bank_open_blob_name(Bank* bank, const char* name);

/// Returns the size of the open blob.
///
/// @param bank bank with an open account and blob
/// @return size in bytes, or 0 when no blob is open
int32_t bank_blob_size(const Bank* bank);

/// Moves the open blob's read/write position.
///
/// @param[in,out] bank bank with an open account and blob; nothing happens without one
/// @param position byte offset, clamped to [0, size]
void bank_blob_seek(Bank* bank, int32_t position);

/// Reads bytes from the open blob at its position and advances it.
///
/// @param[in,out] bank bank with an open account and blob
/// @param[out] out destination for up to `bytes` bytes
/// @param bytes number of bytes wanted
/// @return number of bytes read: fewer at the end of the blob, 0 when no blob is open
uint32_t bank_blob_read(Bank* bank, void* out, uint32_t bytes);

/// Writes bytes into the open blob at its position, growing the blob, and advances it.
///
/// @param[in,out] bank bank with an open account and blob
/// @param data bytes to write
/// @param bytes number of bytes
/// @return `bytes`, or 0 when no blob is open, the blob would pass 2 GiB or it
///     cannot grow
uint32_t bank_blob_write(Bank* bank, const void* data, uint32_t bytes);

// On-disk layout.
inline constexpr char bank_magic[8] = {'H', 'A', 'P', 'I', 'B', 'A', 'N', 'K'};
inline constexpr uint32_t bank_header_bytes = 0x22;
inline constexpr uint32_t bank_format_version = 1;
inline constexpr uint32_t account_header_bytes = 0x20;
inline constexpr uint32_t int_entry_bytes = 8;
inline constexpr uint32_t real_entry_bytes = 12;
inline constexpr uint32_t text_entry_bytes = 8;
inline constexpr uint32_t blob_entry_bytes = 16;

namespace bank_header {
inline constexpr std::size_t magic = 0x0, description = 0x8, pool_offset = 0xc,
                             first_account = 0x10, version = 0x14, pool_packed = 0x18;
} // namespace bank_header

namespace account_header {
inline constexpr std::size_t size = 0x0, name = 0x4, int_count = 0x8, real_count = 0xc,
                             text_count = 0x10, blob_count = 0x14, packed = 0x18;
} // namespace account_header

/// Serializes a bank as a HAPIBANK file image.
///
/// The header comes first, then one record per account (accounts with
/// neither fields nor blobs are omitted), then the string pool, which is
/// LZ77-packed when that is smaller.
///
/// @param bank bank to write; must hold at least one account
/// @param description description stored in the pool; null counts as ""
/// @param pack_accounts true to LZ77-pack each account payload that gets smaller
/// @param[out] out image; its previous contents are replaced
/// @return false when the bank is empty or a buffer cannot grow
/// @quirk The pool's packing budget uses the zlib percentage although LZ77 packs it.
bool bank_write_image(
    const Bank* bank, const char* description, bool pack_accounts, ByteImage* out
);

/// Appends one account record to an image; its names and strings go to a pool.
///
/// The record holds a 32-byte header, then the integer, double, string and
/// blob entry tables and the blob bytes; blob data offsets are file positions
/// of the unpacked record. Blobs of size 0 are left out.
///
/// @param bank bank holding the account
/// @param account account index
/// @param[in,out] file image to append to
/// @param[in,out] pool string pool that collects the account's strings
/// @param pack true to LZ77-pack the payload when that is smaller
/// @return false when a buffer cannot grow; an account with neither fields nor
///     blobs writes nothing and succeeds
/// @quirk The id slot of a named blob entry repeats the name offset of the
///     previous double or the id of the previous id-addressed blob, whichever
///     came last (0 before either).
bool bank_write_account(
    const Bank* bank, int32_t account, ByteImage* file, ByteImage* pool, bool pack
);

/// Appends a NUL-terminated string to a pool.
///
/// @param[in,out] pool string pool
/// @param text text to append, with its terminator
/// @return the string's offset in the pool, or -1 when the pool cannot grow
int32_t bank_pool_append(ByteImage* pool, const char* text);

/// Formats the human-readable account listing written next to a save on request.
///
/// Lists every account with its fields (value and type) and blobs (id or name
/// and size). Line ends are CRLF, as in 3.1c's listing.
///
/// @param bank bank to list
/// @param[out] out text; its previous contents are replaced
/// @return false when the bank has no account table or the buffer cannot grow
bool bank_format_audit(const Bank* bank, ByteImage* out);

struct BankError {
    char message[160];
};

/// Replaces a bank with the accounts of a HAPIBANK image.
///
/// The header, layout and (optionally) description are checked before the
/// bank is reset; the string pool is unpacked when packed.
///
/// @param[in,out] bank bank to replace
/// @param image file image
/// @param size image size in bytes
/// @param description required description, compared case-insensitively; null
///     accepts any
/// @param only_account when not null, the only account read; the others are skipped
/// @param[out] error receives a message on failure (cleared first); may be null
/// @return false when the image is not a version-1 HAPIBANK, is out of range,
///     fails to unpack, has another description or holds a malformed account
bool bank_read_image(
    Bank* bank,
    const uint8_t* image,
    uint32_t size,
    const char* description,
    const char* only_account,
    BankError* error
);

// Platform file boundary.
struct FileSink {
    void* context;
    bool (*write_file)(void* context, const char* path, const uint8_t* data, std::size_t size);
};

struct FileSource {
    void* context;
    bool (*read_file)(void* context, const char* path, ByteImage* out);
};

/// Returns a file sink that writes whole files through C stdio.
///
/// @return the sink; it needs no context
FileSink stdio_file_sink();

/// Returns a file source that reads whole files (up to 1 GiB) through C stdio.
///
/// @return the source; it needs no context
FileSource stdio_file_source();

/// Writes a bank to a file.
///
/// With `audit`, the audit listing is written first beside it (same name with
/// its extension replaced by .cpa); a failed audit write is ignored.
///
/// @param bank bank to write; must hold at least one account
/// @param path file path
/// @param description description stored in the file
/// @param pack_accounts true to LZ77-pack account payloads
/// @param audit true to write the .cpa listing too
/// @param files file boundary
/// @return false when the bank is empty, an argument is null, or the image
///     cannot be built or written
bool bank_write_file(
    const Bank* bank,
    const char* path,
    const char* description,
    bool pack_accounts,
    bool audit,
    const FileSink* files
);

/// Replaces a bank with the accounts of a HAPIBANK file.
///
/// @param[in,out] bank bank to replace
/// @param path file path
/// @param description required description (see bank_read_image); null accepts any
/// @param only_account when not null, the only account read
/// @param files file boundary
/// @param[out] error receives a message on failure; may be null
/// @return false when the file cannot be read or bank_read_image fails
bool bank_read_file(
    Bank* bank,
    const char* path,
    const char* description,
    const char* only_account,
    const FileSource* files,
    BankError* error
);

// Description string the game stores in every save.
inline constexpr const char* savegame_description = "Total Annihilation 3.0";

} // namespace oa::data::persist
