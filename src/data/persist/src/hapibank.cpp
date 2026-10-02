// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/persist/hapibank.hpp"

#include "bank_util.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace oa::data::persist {
namespace {

BankAccount* open_account(const Bank* bank) {
    if (bank == nullptr || bank->accounts == nullptr)
        return nullptr;
    BankAccounts* accounts = bank->accounts;
    if (accounts->open < 0 || accounts->open >= accounts->count)
        return nullptr;
    return &accounts->items[accounts->open];
}

BankBlob* open_blob(const Bank* bank) {
    BankAccount* account = open_account(bank);
    if (account == nullptr || account->open_blob < 0 || account->open_blob >= account->blob_count)
        return nullptr;
    return &account->blobs[account->open_blob];
}

template <class T>
T* grow_array(T* items, int32_t count) {
    return static_cast<T*>(std::realloc(items, static_cast<std::size_t>(count) * sizeof(T)));
}

// Field slot for a setter: frees any previous string value.
BankField* prepare_field(Bank* bank, const char* name) {
    BankAccount* account = open_account(bank);
    if (account == nullptr)
        return nullptr;
    const int32_t index = bank_find_field(bank, name, true);
    if (index < 0)
        return nullptr;
    BankField* field = &account->fields[index];
    if (field->type == BankFieldType::text) {
        std::free(field->text);
        field->text = nullptr;
    }
    return field;
}

const BankField* typed_field(Bank* bank, const char* name, BankFieldType type) {
    BankAccount* account = open_account(bank);
    if (account == nullptr)
        return nullptr;
    const int32_t index = bank_find_field(bank, name, false);
    if (index < 0 || account->fields[index].type != type)
        return nullptr;
    return &account->fields[index];
}

} // namespace

void bank_init(Bank* bank) {
    bank->accounts = nullptr;
}

void bank_destroy(Bank* bank) {
    BankAccounts* accounts = bank->accounts;
    if (accounts == nullptr)
        return;
    for (int32_t i = 0; i < accounts->count; ++i) {
        BankAccount& account = accounts->items[i];
        std::free(account.name);
        for (int32_t f = 0; f < account.field_count; ++f) {
            std::free(account.fields[f].name);
            if (account.fields[f].type == BankFieldType::text)
                std::free(account.fields[f].text);
        }
        std::free(account.fields);
        for (int32_t b = 0; b < account.blob_count; ++b) {
            std::free(account.blobs[b].name);
            std::free(account.blobs[b].data);
        }
        std::free(account.blobs);
    }
    std::free(accounts->items);
    std::free(accounts);
    bank->accounts = nullptr;
}

bool bank_reset(Bank* bank) {
    bank_destroy(bank);
    auto* accounts = static_cast<BankAccounts*>(std::calloc(1, sizeof(BankAccounts)));
    if (accounts == nullptr)
        return false;
    accounts->open = -1;
    bank->accounts = accounts;
    return true;
}

bool bank_open_account(Bank* bank, const char* name) {
    if (bank == nullptr || bank->accounts == nullptr || name == nullptr)
        return false;
    BankAccounts* accounts = bank->accounts;
    for (int32_t i = 0; i < accounts->count; ++i) {
        if (detail::equal_nocase(accounts->items[i].name, name)) {
            accounts->open = i;
            accounts->items[i].open_blob = -1;
            return true;
        }
    }
    char* copy = detail::duplicate(name);
    BankAccount* items =
        copy != nullptr ? grow_array(accounts->items, accounts->count + 1) : nullptr;
    if (items == nullptr) {
        std::free(copy);
        accounts->open = -1;
        return false;
    }
    accounts->items = items;
    accounts->open = accounts->count++;
    BankAccount& account = items[accounts->open];
    std::memset(static_cast<void*>(&account), 0, sizeof(account));
    account.name = copy;
    account.open_blob = -1;
    return false;
}

int32_t bank_find_field(Bank* bank, const char* name, bool create) {
    BankAccount* account = open_account(bank);
    if (account == nullptr || name == nullptr)
        return -1;
    for (int32_t i = 0; i < account->field_count; ++i)
        if (detail::equal_nocase(account->fields[i].name, name))
            return i;
    if (!create)
        return -1;
    char* copy = detail::duplicate(name);
    BankField* fields =
        copy != nullptr ? grow_array(account->fields, account->field_count + 1) : nullptr;
    if (fields == nullptr) {
        std::free(copy);
        return -1;
    }
    account->fields = fields;
    const int32_t index = account->field_count++;
    std::memset(static_cast<void*>(&fields[index]), 0, sizeof(BankField));
    fields[index].name = copy;
    return index;
}

bool bank_has_field(Bank* bank, const char* name) {
    return bank_find_field(bank, name, false) >= 0;
}

bool bank_set_int(Bank* bank, const char* name, int32_t value) {
    BankField* field = prepare_field(bank, name);
    if (field == nullptr)
        return false;
    field->integer = value;
    field->type = BankFieldType::integer;
    return true;
}

bool bank_set_real(Bank* bank, const char* name, double value) {
    BankField* field = prepare_field(bank, name);
    if (field == nullptr)
        return false;
    field->real = value;
    field->type = BankFieldType::real;
    return true;
}

bool bank_set_text(Bank* bank, const char* name, const char* value) {
    if (value == nullptr)
        return false;
    char* copy = detail::duplicate(value);
    if (copy == nullptr)
        return false;
    BankField* field = prepare_field(bank, name);
    if (field == nullptr) {
        std::free(copy);
        return false;
    }
    field->text = copy;
    field->type = BankFieldType::text;
    return true;
}

int32_t bank_get_int(Bank* bank, const char* name, int32_t fallback) {
    const BankField* field = typed_field(bank, name, BankFieldType::integer);
    return field != nullptr ? field->integer : fallback;
}

double bank_get_real(Bank* bank, const char* name, double fallback) {
    const BankField* field = typed_field(bank, name, BankFieldType::real);
    return field != nullptr ? field->real : fallback;
}

const char* bank_get_text(Bank* bank, const char* name, const char* fallback) {
    const BankField* field = typed_field(bank, name, BankFieldType::text);
    return field != nullptr ? field->text : fallback;
}

int32_t bank_find_blob_id(Bank* bank, int32_t id, bool create) {
    BankAccount* account = open_account(bank);
    if (account == nullptr)
        return -1;
    for (int32_t i = 0; i < account->blob_count; ++i)
        if (account->blobs[i].named == 0 && account->blobs[i].id == id)
            return i;
    if (!create)
        return -1;
    BankBlob* blobs = grow_array(account->blobs, account->blob_count + 1);
    if (blobs == nullptr)
        return -1;
    account->blobs = blobs;
    const int32_t index = account->blob_count++;
    std::memset(static_cast<void*>(&blobs[index]), 0, sizeof(BankBlob));
    blobs[index].id = id;
    return index;
}

int32_t bank_find_blob_name(Bank* bank, const char* name, bool create) {
    BankAccount* account = open_account(bank);
    if (account == nullptr || name == nullptr)
        return -1;
    for (int32_t i = 0; i < account->blob_count; ++i)
        if (account->blobs[i].named != 0 && detail::equal_nocase(account->blobs[i].name, name))
            return i;
    if (!create)
        return -1;
    char* copy = detail::duplicate(name);
    BankBlob* blobs =
        copy != nullptr ? grow_array(account->blobs, account->blob_count + 1) : nullptr;
    if (blobs == nullptr) {
        std::free(copy);
        return -1;
    }
    account->blobs = blobs;
    const int32_t index = account->blob_count++;
    std::memset(static_cast<void*>(&blobs[index]), 0, sizeof(BankBlob));
    blobs[index].name = copy;
    blobs[index].named = 1;
    return index;
}

bool bank_open_blob_id(Bank* bank, int32_t id) {
    BankAccount* account = open_account(bank);
    if (account == nullptr)
        return false;
    account->open_blob = bank_find_blob_id(bank, id, true);
    const BankBlob* blob = open_blob(bank);
    return blob != nullptr && blob->data != nullptr;
}

bool bank_open_blob_name(Bank* bank, const char* name) {
    BankAccount* account = open_account(bank);
    if (account == nullptr)
        return false;
    account->open_blob = bank_find_blob_name(bank, name, true);
    const BankBlob* blob = open_blob(bank);
    return blob != nullptr && blob->data != nullptr;
}

int32_t bank_blob_size(const Bank* bank) {
    const BankBlob* blob = open_blob(bank);
    return blob != nullptr ? blob->size : 0;
}

void bank_blob_seek(Bank* bank, int32_t position) {
    BankBlob* blob = open_blob(bank);
    if (blob == nullptr)
        return;
    if (position < 0)
        position = 0;
    if (position > blob->size)
        position = blob->size;
    blob->position = position;
}

uint32_t bank_blob_read(Bank* bank, void* out, uint32_t bytes) {
    BankBlob* blob = open_blob(bank);
    if (blob == nullptr)
        return 0;
    const int32_t available = blob->size - blob->position;
    if (available < 1)
        return 0;
    if (available < static_cast<int32_t>(bytes))
        bytes = static_cast<uint32_t>(available);
    std::memmove(out, blob->data + blob->position, bytes);
    blob->position += static_cast<int32_t>(bytes);
    return bytes;
}

uint32_t bank_blob_write(Bank* bank, const void* data, uint32_t bytes) {
    BankBlob* blob = open_blob(bank);
    if (blob == nullptr)
        return 0;
    const int64_t end = static_cast<int64_t>(blob->position) + bytes;
    if (end > INT32_MAX)
        return 0;
    if (blob->size < end) {
        auto* grown =
            static_cast<uint8_t*>(std::realloc(blob->data, static_cast<std::size_t>(end)));
        if (grown == nullptr)
            return 0;
        blob->data = grown;
        blob->size = static_cast<int32_t>(end);
    }
    if (bytes != 0)
        std::memmove(blob->data + blob->position, data, bytes);
    blob->position += static_cast<int32_t>(bytes);
    return bytes;
}

} // namespace oa::data::persist
