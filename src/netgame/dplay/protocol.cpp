// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/netgame/dplay/protocol.hpp"

#include <cstdint>
#include <cstring>

namespace oa::netgame::dplay {
namespace {

constexpr std::size_t packed_player_fixed_bytes = 48;
constexpr std::size_t super_packed_fixed_bytes = 16;
constexpr std::size_t addresses_bytes = 2 * sockaddr_bytes;
constexpr std::size_t security_desc_bytes = 24;
constexpr std::size_t create_player_reserved_bytes = 6;
constexpr uint32_t player_message_create_offset = 0x1c; // play header + five dwords
/// Offset, from the "play" signature, of what follows a name or data
/// change's four dwords: the play header and 16 bytes.
constexpr uint32_t player_change_body_offset = 0x18;

// Super-packed info mask: presence bits and 2-bit length-field size codes.
constexpr uint32_t mask_short_name = 0x1;
constexpr uint32_t mask_long_name = 0x2;
constexpr unsigned mask_sp_length_shift = 2;
constexpr unsigned mask_data_length_shift = 4;
constexpr unsigned mask_player_count_shift = 6;
constexpr uint32_t mask_parent_id = 0x100;
constexpr unsigned mask_shortcut_count_shift = 9;

struct Writer {
    uint8_t* out = nullptr;
    std::size_t capacity = 0;
    std::size_t at = 0;
    bool ok = false;

    bool room(std::size_t n) {
        if (!ok || capacity - at < n || at > capacity)
            ok = false;
        return ok;
    }

    void u8(uint8_t v) {
        if (room(1))
            out[at++] = v;
    }

    void u16(uint16_t v) {
        if (room(2)) {
            store_u16(out + at, v);
            at += 2;
        }
    }

    void u32(uint32_t v) {
        if (room(4)) {
            store_u32(out + at, v);
            at += 4;
        }
    }

    void bytes(const uint8_t* p, std::size_t n) {
        if (n != 0 && room(n)) {
            std::memcpy(out + at, p, n);
            at += n;
        }
    }

    void zeros(std::size_t n) {
        if (room(n)) {
            std::memset(out + at, 0, n);
            at += n;
        }
    }

    void sockaddr(const Address& a) {
        if (room(sockaddr_bytes)) {
            store_sockaddr(out + at, a);
            at += sockaddr_bytes;
        }
    }

    // ANSI to UTF-16LE with terminator.
    void utf16z(const char* s) {
        for (std::size_t i = 0; s != nullptr && s[i] != '\0'; ++i)
            u16(static_cast<uint8_t>(s[i]));
        u16(0);
    }
};

std::size_t utf16z_bytes(const char* s) {
    return 2 * ((s == nullptr ? 0 : std::strlen(s)) + 1);
}

Writer begin_message(const Address& reply_to, uint16_t cmd, uint8_t* out, std::size_t capacity) {
    Writer w{out, capacity, 0, out != nullptr};
    w.u32(0);
    w.sockaddr(reply_to);
    w.u32(dplay_play_signature);
    w.u16(cmd);
    w.u16(protocol_version);
    return w;
}

std::size_t finish(Writer& w) {
    if (!w.ok || w.at > dplay_envelope_size_mask)
        return 0;
    store_u32(w.out, (dplay_envelope_token << 20) | static_cast<uint32_t>(w.at));
    return w.at;
}

void write_desc(Writer& w, const SessionDesc& desc) {
    if (w.room(session_desc_bytes)) {
        (void)encode_session_desc(desc, w.out + w.at, session_desc_bytes);
        w.at += session_desc_bytes;
    }
}

std::size_t length_code(std::size_t n) {
    return n < 0x100 ? 1 : n < 0x10000 ? 2 : 3;
}

void write_sized(Writer& w, std::size_t code, std::size_t n) {
    if (code == 1)
        w.u8(static_cast<uint8_t>(n));
    else if (code == 2)
        w.u16(static_cast<uint16_t>(n));
    else
        w.u32(static_cast<uint32_t>(n));
}

std::size_t name_bytes(bool present, const char* name) {
    return present ? utf16z_bytes(name) : 0;
}

void write_packed_player(Writer& w, const PlayerInfo& p) {
    const std::size_t short_bytes = name_bytes(p.has_short_name, p.short_name);
    const std::size_t long_bytes = name_bytes(p.has_long_name, p.long_name);
    const std::size_t sp_bytes = p.has_addresses ? addresses_bytes : 0;
    w.u32(
        static_cast<uint32_t>(
            packed_player_fixed_bytes + short_bytes + long_bytes + sp_bytes + p.data_size
        )
    );
    w.u32(p.flags);
    w.u32(p.id);
    w.u32(static_cast<uint32_t>(short_bytes));
    w.u32(static_cast<uint32_t>(long_bytes));
    w.u32(static_cast<uint32_t>(sp_bytes));
    w.u32(p.data_size);
    w.u32(0); // player count (groups only)
    w.u32(p.system_id);
    w.u32(static_cast<uint32_t>(packed_player_fixed_bytes));
    w.u32(p.version);
    w.u32(p.parent_id);
    if (p.has_short_name)
        w.utf16z(p.short_name);
    if (p.has_long_name)
        w.utf16z(p.long_name);
    if (p.has_addresses) {
        w.sockaddr(p.stream);
        w.sockaddr(p.datagram);
    }
    w.bytes(p.data, p.data_size);
}

void write_super_packed_player(Writer& w, const PlayerInfo& p) {
    uint32_t mask = 0;
    if (p.has_short_name)
        mask |= mask_short_name;
    if (p.has_long_name)
        mask |= mask_long_name;
    const std::size_t sp_code = p.has_addresses ? length_code(addresses_bytes) : 0;
    const std::size_t data_code = p.data_size != 0 ? length_code(p.data_size) : 0;
    mask |= static_cast<uint32_t>(sp_code << mask_sp_length_shift);
    mask |= static_cast<uint32_t>(data_code << mask_data_length_shift);
    if (p.parent_id != 0)
        mask |= mask_parent_id;
    w.u32(static_cast<uint32_t>(super_packed_fixed_bytes));
    w.u32(p.flags);
    w.u32(p.id);
    w.u32(mask);
    w.u32((p.flags & player_flag::system_player) != 0 ? p.version : p.system_id);
    if (p.has_short_name)
        w.utf16z(p.short_name);
    if (p.has_long_name)
        w.utf16z(p.long_name);
    if (data_code != 0) {
        write_sized(w, data_code, p.data_size);
        w.bytes(p.data, p.data_size);
    }
    if (sp_code != 0) {
        write_sized(w, sp_code, addresses_bytes);
        w.sockaddr(p.stream);
        w.sockaddr(p.datagram);
    }
    if (p.parent_id != 0)
        w.u32(p.parent_id);
}

// Bounded reader over one message; offsets are absolute within it.
struct Reader {
    const uint8_t* base = nullptr;
    std::size_t size = 0;

    bool has(std::size_t at, std::size_t n) const { return at <= size && size - at >= n; }

    uint32_t u32(std::size_t at) const { return has(at, 4) ? load_u32(base + at) : 0; }
};

// UTF-16LE (terminated or bounded by the message) to ANSI; truncates.
bool read_utf16z(const Reader& r, std::size_t at, char* out, std::size_t out_chars) {
    std::size_t n = 0;
    out[0] = '\0';
    for (; r.has(at, 2); at += 2) {
        const uint16_t c = load_u16(r.base + at);
        if (c == 0)
            return true;
        if (n < out_chars)
            out[n++] = c <= 0xff ? static_cast<char>(c) : '?';
        out[n] = '\0';
    }
    return false;
}

// Offset relative to the "play" signature -> absolute, 0 meaning absent.
bool play_offset(const Reader& r, uint32_t offset, std::size_t* at) {
    // Compared before adding, so a 32-bit size cannot wrap.
    if (offset == 0 || r.size <= dplay_envelope_bytes || offset >= r.size - dplay_envelope_bytes)
        return false;
    *at = dplay_envelope_bytes + static_cast<std::size_t>(offset);
    return true;
}

void read_addresses(const uint8_t* p, std::size_t sp_bytes, PlayerInfo* out) {
    if (sp_bytes >= sockaddr_bytes) {
        out->has_addresses = true;
        out->stream = load_sockaddr(p);
        if (sp_bytes >= addresses_bytes)
            out->datagram = load_sockaddr(p + sockaddr_bytes);
    }
}

ParseError read_packed_player(const Reader& r, std::size_t at, PlayerInfo* out) {
    if (!r.has(at, packed_player_fixed_bytes))
        return ParseError::truncated;
    PlayerInfo p{};
    p.flags = r.u32(at + 4);
    p.id = r.u32(at + 8);
    const std::size_t short_bytes = r.u32(at + 12);
    const std::size_t long_bytes = r.u32(at + 16);
    const std::size_t sp_bytes = r.u32(at + 20);
    const std::size_t data_bytes = r.u32(at + 24);
    p.system_id = r.u32(at + 32);
    const std::size_t fixed = r.u32(at + 36);
    p.version = r.u32(at + 40);
    p.parent_id = r.u32(at + 44);
    if (fixed < packed_player_fixed_bytes || !r.has(at, fixed) || short_bytes > max_message_bytes ||
        long_bytes > max_message_bytes || sp_bytes > max_message_bytes ||
        data_bytes > max_message_bytes)
        return ParseError::bad_offset;
    std::size_t pos = at + fixed;
    if (!r.has(pos, short_bytes + long_bytes + sp_bytes + data_bytes))
        return ParseError::truncated;
    if (data_bytes > max_player_data_bytes)
        return ParseError::unsupported;
    const Reader short_reader{r.base, pos + short_bytes};
    if (short_bytes != 0)
        p.has_short_name = true, (void)read_utf16z(short_reader, pos, p.short_name, max_name_chars);
    pos += short_bytes;
    const Reader long_reader{r.base, pos + long_bytes};
    if (long_bytes != 0)
        p.has_long_name = true, (void)read_utf16z(long_reader, pos, p.long_name, max_name_chars);
    pos += long_bytes;
    read_addresses(r.base + pos, sp_bytes, &p);
    pos += sp_bytes;
    p.data_size = static_cast<uint16_t>(data_bytes);
    std::memcpy(p.data, r.base + pos, data_bytes);
    *out = p;
    return ParseError::ok;
}

bool read_sized(const Reader& r, std::size_t* at, uint32_t code, std::size_t* value) {
    const std::size_t n = code == 1 ? 1 : code == 2 ? 2 : 4;
    if (!r.has(*at, n))
        return false;
    *value = n == 1 ? r.base[*at] : n == 2 ? load_u16(r.base + *at) : load_u32(r.base + *at);
    *at += n;
    return true;
}

bool skip_utf16z(const Reader& r, std::size_t* at, char* out) {
    if (!read_utf16z(r, *at, out, max_name_chars))
        return false;
    while (load_u16(r.base + *at) != 0)
        *at += 2;
    *at += 2;
    return true;
}

ParseError read_super_packed_player(const Reader& r, std::size_t* at, PlayerInfo* out) {
    std::size_t pos = *at;
    if (!r.has(pos, super_packed_fixed_bytes))
        return ParseError::truncated;
    PlayerInfo p{};
    const std::size_t fixed = r.u32(pos);
    p.flags = r.u32(pos + 4);
    p.id = r.u32(pos + 8);
    const uint32_t mask = r.u32(pos + 12);
    if (fixed < super_packed_fixed_bytes || !r.has(pos, fixed) || !r.has(pos + fixed, 4))
        return ParseError::bad_offset;
    pos += fixed;
    if ((p.flags & player_flag::system_player) != 0) {
        p.version = r.u32(pos);
        p.system_id = p.id;
    } else {
        p.system_id = r.u32(pos);
    }
    pos += 4;
    if ((mask & mask_short_name) != 0 && !(p.has_short_name = skip_utf16z(r, &pos, p.short_name)))
        return ParseError::truncated;
    if ((mask & mask_long_name) != 0 && !(p.has_long_name = skip_utf16z(r, &pos, p.long_name)))
        return ParseError::truncated;
    std::size_t n = 0;
    if (const auto code = (mask >> mask_data_length_shift) & 3; code != 0) {
        if (!read_sized(r, &pos, code, &n) || !r.has(pos, n))
            return ParseError::truncated;
        if (n > max_player_data_bytes)
            return ParseError::unsupported;
        p.data_size = static_cast<uint16_t>(n);
        std::memcpy(p.data, r.base + pos, n);
        pos += n;
    }
    if (const auto code = (mask >> mask_sp_length_shift) & 3; code != 0) {
        if (!read_sized(r, &pos, code, &n) || !r.has(pos, n))
            return ParseError::truncated;
        read_addresses(r.base + pos, n, &p);
        pos += n;
    }
    if (const auto code = (mask >> mask_player_count_shift) & 3; code != 0) {
        if (!read_sized(r, &pos, code, &n) || n > max_message_bytes / 4 || !r.has(pos, 4 * n))
            return ParseError::truncated;
        pos += 4 * n;
    }
    if ((mask & mask_parent_id) != 0) {
        if (!r.has(pos, 4))
            return ParseError::truncated;
        p.parent_id = r.u32(pos);
        pos += 4;
    }
    if (const auto code = (mask >> mask_shortcut_count_shift) & 3; code != 0) {
        if (!read_sized(r, &pos, code, &n) || n > max_message_bytes / 4 || !r.has(pos, 4 * n))
            return ParseError::truncated;
        pos += 4 * n;
    }
    *at = pos;
    *out = p;
    return ParseError::ok;
}

void read_optional_string(const Reader& r, uint32_t offset, char* out) {
    std::size_t at = 0;
    if (play_offset(r, offset, &at))
        (void)read_utf16z(r, at, out, max_name_chars);
}

ParseError read_player_message(const Reader& r, Message* m) {
    constexpr std::size_t body = header_bytes;
    if (!r.has(body, 20))
        return ParseError::truncated;
    m->id_to = r.u32(body);
    m->player_id = r.u32(body + 4);
    m->group_id = r.u32(body + 8);
    std::size_t at = 0;
    if (!play_offset(r, r.u32(body + 12), &at))
        return ParseError::bad_offset;
    if (const auto e = read_packed_player(r, at, &m->player); e != ParseError::ok)
        return e;
    std::size_t password_at = 0;
    if (play_offset(r, r.u32(body + 16), &password_at)) {
        (void)read_utf16z(r, password_at, m->password, max_name_chars);
        std::size_t end = password_at;
        while (r.has(end, 2) && load_u16(r.base + end) != 0)
            end += 2;
        if (r.has(end + 2, 4))
            m->tick = r.u32(end + 2);
    }
    return ParseError::ok;
}

ParseError read_session_desc_at(const Reader& r, std::size_t at, SessionDesc* out) {
    if (!r.has(at, session_desc_bytes))
        return ParseError::truncated;
    return decode_session_desc(r.base + at, session_desc_bytes, out) == WireError::ok
               ? ParseError::ok
               : ParseError::truncated;
}

} // namespace

const char* result_name(uint32_t hr) noexcept {
    switch (hr) {
    case result::ok:
        return "DP_OK";
    case result::unsupported:
        return "DPERR_UNSUPPORTED";
    case result::generic:
        return "DPERR_GENERIC";
    case result::out_of_memory:
        return "DPERR_OUTOFMEMORY";
    case result::invalid_params:
        return "DPERR_INVALIDPARAMS";
    case result::already_initialized:
        return "DPERR_ALREADYINITIALIZED";
    case result::access_denied:
        return "DPERR_ACCESSDENIED";
    case result::active_players:
        return "DPERR_ACTIVEPLAYERS";
    case result::buffer_too_small:
        return "DPERR_BUFFERTOOSMALL";
    case result::cant_add_player:
        return "DPERR_CANTADDPLAYER";
    case result::cant_create_player:
        return "DPERR_CANTCREATEPLAYER";
    case 0x88770050:
        return "DPERR_CAPSNOTAVAILABLEYET";
    case 0x8877005a:
        return "DPERR_EXCEPTION";
    case result::invalid_flags:
        return "DPERR_INVALIDFLAGS";
    case result::invalid_object:
        return "DPERR_INVALIDOBJECT";
    case result::invalid_player:
        return "DPERR_INVALIDPLAYER";
    case 0x887700a0:
        return "DPERR_NOCAPS";
    case result::no_connection:
        return "DPERR_NOCONNECTION";
    case result::no_messages:
        return "DPERR_NOMESSAGES";
    case result::no_name_server_found:
        return "DPERR_NONAMESERVERFOUND";
    case 0x887700d2:
        return "DPERR_NOPLAYERS";
    case result::no_sessions:
        return "DPERR_NOSESSIONS";
    case result::send_too_big:
        return "DPERR_SENDTOOBIG";
    case result::timeout:
        return "DPERR_TIMEOUT";
    case 0x887700fa:
        return "DPERR_UNAVAILABLE";
    case 0x8877010e:
        return "DPERR_BUSY";
    case result::user_cancel:
        return "DPERR_USERCANCEL";
    case result::session_lost:
        return "DPERR_SESSIONLOST";
    case result::uninitialized:
        return "DPERR_UNINITIALIZED";
    default:
        return nullptr;
    }
}

bool guid_equal(const Guid& a, const Guid& b) noexcept {
    return std::memcmp(a.bytes, b.bytes, sizeof a.bytes) == 0;
}

bool guid_is_zero(const Guid& g) noexcept {
    return guid_equal(g, Guid{});
}

bool address_equal(const Address& a, const Address& b) noexcept {
    return a.port == b.port && std::memcmp(a.ip, b.ip, 4) == 0;
}

bool address_ip_is_zero(const Address& a) noexcept {
    return (a.ip[0] | a.ip[1] | a.ip[2] | a.ip[3]) == 0;
}

void store_sockaddr(uint8_t* out, const Address& in) noexcept {
    store_u16(out, dplay_address_family_inet);
    out[2] = static_cast<uint8_t>(in.port >> 8);
    out[3] = static_cast<uint8_t>(in.port);
    std::memcpy(out + 4, in.ip, 4);
    std::memset(out + 8, 0, 8);
}

Address load_sockaddr(const uint8_t* in) noexcept {
    Address a{};
    a.port = static_cast<uint16_t>((in[2] << 8) | in[3]);
    std::memcpy(a.ip, in + 4, 4);
    return a;
}

std::size_t stream_message_size(const uint8_t* bytes) noexcept {
    const uint32_t word = load_u32(bytes);
    const std::size_t size = word & dplay_envelope_size_mask;
    if ((word >> 20) != dplay_envelope_token || size < dplay_envelope_bytes ||
        size > max_message_bytes)
        return 0;
    return size;
}

ParseError decode_message(const uint8_t* bytes, std::size_t size, Message* out) noexcept {
    if (bytes == nullptr || out == nullptr)
        return ParseError::truncated;
    if (size < 4 || (load_u32(bytes) >> 20) != dplay_envelope_token)
        return ParseError::not_dplay;
    const std::size_t declared = load_u32(bytes) & dplay_envelope_size_mask;
    if (declared < dplay_envelope_bytes || declared > size)
        return ParseError::truncated;
    const Reader r{bytes, declared};
    if (!r.has(0, header_bytes) || r.u32(dplay_envelope_bytes) != dplay_play_signature)
        return ParseError::not_control;
    Message* m = out;
    std::memset(static_cast<void*>(m), 0, sizeof *m);
    m->reply_to = load_sockaddr(bytes + 4);
    m->command = load_u16(bytes + 24);
    m->version = load_u16(bytes + 26);
    constexpr std::size_t body = header_bytes;
    switch (m->command) {
    case command::enum_sessions:
        if (!r.has(body, 24))
            return ParseError::truncated;
        std::memcpy(m->application.bytes, bytes + body, 16);
        read_optional_string(r, r.u32(body + 16), m->password);
        m->flags = r.u32(body + 20);
        return ParseError::ok;
    case command::enum_sessions_reply:
        if (const auto e = read_session_desc_at(r, body, &m->desc); e != ParseError::ok)
            return e;
        read_optional_string(r, r.u32(body + session_desc_bytes), m->session_name);
        return ParseError::ok;
    case command::request_player_id:
        if (!r.has(body, 4))
            return ParseError::truncated;
        m->flags = r.u32(body);
        return ParseError::ok;
    case command::request_player_reply:
        if (!r.has(body, 4 + security_desc_bytes + 12))
            return ParseError::truncated;
        m->player_id = r.u32(body);
        m->result = r.u32(body + 4 + security_desc_bytes + 8);
        return ParseError::ok;
    case command::create_player:
    case command::add_forward_request:
    case command::add_forward:
        return read_player_message(r, m);
    case command::delete_player:
        if (!r.has(body, 12))
            return ParseError::truncated;
        m->id_to = r.u32(body);
        m->player_id = r.u32(body + 4);
        m->group_id = r.u32(body + 8);
        return ParseError::ok;
    case command::add_forward_reply:
        if (!r.has(body, 4))
            return ParseError::truncated;
        m->result = r.u32(body);
        return ParseError::ok;
    case command::add_forward_ack:
        if (!r.has(body, 4))
            return ParseError::truncated;
        m->player_id = r.u32(body);
        return ParseError::ok;
    case command::ping:
    case command::ping_reply:
        if (!r.has(body, 8))
            return ParseError::truncated;
        m->player_id = r.u32(body);
        m->tick = r.u32(body + 4);
        return ParseError::ok;
    case command::you_are_dead:
        return ParseError::ok;
    case command::i_am_name_server: {
        if (!r.has(body, 16))
            return ParseError::truncated;
        m->id_to = r.u32(body);
        m->player_id = r.u32(body + 4);
        m->flags = r.u32(body + 8);
        const std::size_t sp_bytes = r.u32(body + 12);
        if (sp_bytes > max_message_bytes || !r.has(body + 16, sp_bytes))
            return ParseError::truncated;
        read_addresses(bytes + body + 16, sp_bytes, &m->player);
        return ParseError::ok;
    }
    case command::player_data_changed: {
        if (!r.has(body, 16))
            return ParseError::truncated;
        m->id_to = r.u32(body);
        m->player_id = r.u32(body + 4);
        const std::size_t data_bytes = r.u32(body + 8);
        if (data_bytes == 0)
            return ParseError::ok;
        std::size_t at = 0;
        if (!play_offset(r, r.u32(body + 12), &at))
            return ParseError::bad_offset;
        if (data_bytes > max_message_bytes || !r.has(at, data_bytes))
            return ParseError::truncated;
        if (data_bytes > max_player_data_bytes)
            return ParseError::unsupported;
        m->player.data_size = static_cast<uint16_t>(data_bytes);
        std::memcpy(m->player.data, bytes + at, data_bytes);
        return ParseError::ok;
    }
    case command::player_name_changed: {
        if (!r.has(body, 16))
            return ParseError::truncated;
        m->id_to = r.u32(body);
        m->player_id = r.u32(body + 4);
        // A name offset outside the message, or a name without its
        // terminator, makes the message malformed; an offset of 0 is no name.
        const uint32_t short_offset = r.u32(body + 8);
        const uint32_t long_offset = r.u32(body + 12);
        std::size_t at = 0;
        if (short_offset != 0) {
            if (!play_offset(r, short_offset, &at))
                return ParseError::bad_offset;
            if (!read_utf16z(r, at, m->player.short_name, max_name_chars))
                return ParseError::truncated;
            m->player.has_short_name = true;
        }
        if (long_offset != 0) {
            if (!play_offset(r, long_offset, &at))
                return ParseError::bad_offset;
            if (!read_utf16z(r, at, m->player.long_name, max_name_chars))
                return ParseError::truncated;
            m->player.has_long_name = true;
        }
        return ParseError::ok;
    }
    case command::session_desc_changed:
        if (!r.has(body, 12))
            return ParseError::truncated;
        m->id_to = r.u32(body);
        if (const auto e = read_session_desc_at(r, body + 12, &m->desc); e != ParseError::ok)
            return e;
        read_optional_string(r, r.u32(body + 4), m->session_name);
        read_optional_string(r, r.u32(body + 8), m->password);
        return ParseError::ok;
    case command::super_enum_players_reply: {
        if (!r.has(body, 28))
            return ParseError::truncated;
        const uint32_t players = r.u32(body);
        std::size_t desc_at = 0;
        if (!play_offset(r, r.u32(body + 16), &desc_at))
            return ParseError::bad_offset;
        if (const auto e = read_session_desc_at(r, desc_at, &m->desc); e != ParseError::ok)
            return e;
        read_optional_string(r, r.u32(body + 20), m->session_name);
        read_optional_string(r, r.u32(body + 24), m->password);
        if (players > max_roster_players)
            return ParseError::too_many_players;
        std::size_t at = 0;
        if (players != 0 && !play_offset(r, r.u32(body + 8), &at))
            return ParseError::bad_offset;
        for (uint32_t i = 0; i < players; ++i)
            if (const auto e = read_super_packed_player(r, &at, &m->roster[i]); e != ParseError::ok)
                return e;
        m->roster_count = players;
        return ParseError::ok;
    }
    default:
        return ParseError::unsupported;
    }
}

std::size_t encode_enum_sessions(
    const Address& reply_to,
    const Guid& application,
    const char* password,
    uint32_t flags,
    uint8_t* out,
    std::size_t capacity
) noexcept {
    Writer w = begin_message(reply_to, command::enum_sessions, out, capacity);
    const bool has_password = password != nullptr && password[0] != '\0';
    w.bytes(application.bytes, 16);
    w.u32(has_password ? static_cast<uint32_t>(dplay_play_header_bytes + 24) : 0);
    w.u32(flags);
    if (has_password)
        w.utf16z(password);
    return finish(w);
}

std::size_t encode_enum_sessions_reply(
    const Address& reply_to,
    const SessionDesc& desc,
    const char* name,
    uint8_t* out,
    std::size_t capacity
) noexcept {
    Writer w = begin_message(reply_to, command::enum_sessions_reply, out, capacity);
    write_desc(w, desc);
    const bool has_name = name != nullptr;
    w.u32(has_name ? static_cast<uint32_t>(dplay_play_header_bytes + session_desc_bytes + 4) : 0);
    if (has_name)
        w.utf16z(name);
    return finish(w);
}

std::size_t encode_request_player_id(
    const Address& reply_to, uint32_t flags, uint8_t* out, std::size_t capacity
) noexcept {
    Writer w = begin_message(reply_to, command::request_player_id, out, capacity);
    w.u32(flags);
    return finish(w);
}

std::size_t encode_request_player_reply(
    const Address& reply_to, uint32_t id, uint32_t result, uint8_t* out, std::size_t capacity
) noexcept {
    Writer w = begin_message(reply_to, command::request_player_reply, out, capacity);
    w.u32(id);
    w.zeros(security_desc_bytes + 8); // security description, SSPI and CAPI offsets
    w.u32(result);
    return finish(w);
}

std::size_t encode_player_message(
    const Address& reply_to,
    uint16_t cmd,
    uint32_t id_to,
    const PlayerInfo& player,
    const char* password,
    uint32_t tick,
    uint8_t* out,
    std::size_t capacity
) noexcept {
    Writer w = begin_message(reply_to, cmd, out, capacity);
    const bool forward = cmd != command::create_player;
    const std::size_t packed = packed_player_fixed_bytes +
                               name_bytes(player.has_short_name, player.short_name) +
                               name_bytes(player.has_long_name, player.long_name) +
                               (player.has_addresses ? addresses_bytes : 0) + player.data_size;
    w.u32(id_to);
    w.u32(player.id);
    w.u32(0); // group
    w.u32(player_message_create_offset);
    w.u32(forward ? static_cast<uint32_t>(player_message_create_offset + packed) : 0);
    write_packed_player(w, player);
    if (forward) {
        w.utf16z(password);
        w.u32(tick);
    } else {
        w.zeros(create_player_reserved_bytes);
    }
    return finish(w);
}

std::size_t encode_delete_player(
    const Address& reply_to, uint32_t id_to, uint32_t player_id, uint8_t* out, std::size_t capacity
) noexcept {
    Writer w = begin_message(reply_to, command::delete_player, out, capacity);
    w.u32(id_to);
    w.u32(player_id);
    w.zeros(12); // group, create offset, password offset
    return finish(w);
}

std::size_t encode_add_forward_reply(
    const Address& reply_to, uint32_t result, uint8_t* out, std::size_t capacity
) noexcept {
    Writer w = begin_message(reply_to, command::add_forward_reply, out, capacity);
    w.u32(result);
    return finish(w);
}

std::size_t encode_add_forward_ack(
    const Address& reply_to, uint32_t player_id, uint8_t* out, std::size_t capacity
) noexcept {
    Writer w = begin_message(reply_to, command::add_forward_ack, out, capacity);
    w.u32(player_id);
    return finish(w);
}

std::size_t encode_ping(
    const Address& reply_to,
    uint16_t cmd,
    uint32_t from_id,
    uint32_t tick,
    uint8_t* out,
    std::size_t capacity
) noexcept {
    Writer w = begin_message(reply_to, cmd, out, capacity);
    w.u32(from_id);
    w.u32(tick);
    return finish(w);
}

std::size_t encode_i_am_name_server(
    const Address& reply_to,
    uint32_t id_to,
    uint32_t host_id,
    uint32_t flags,
    const Address& stream,
    const Address& datagram,
    uint8_t* out,
    std::size_t capacity
) noexcept {
    Writer w = begin_message(reply_to, command::i_am_name_server, out, capacity);
    w.u32(id_to);
    w.u32(host_id);
    w.u32(flags);
    w.u32(name_server_addresses_bytes);
    w.sockaddr(stream);
    w.sockaddr(datagram);
    return finish(w);
}

std::size_t
encode_you_are_dead(const Address& reply_to, uint8_t* out, std::size_t capacity) noexcept {
    Writer w = begin_message(reply_to, command::you_are_dead, out, capacity);
    return finish(w);
}

std::size_t encode_player_data_changed(
    const Address& reply_to,
    uint32_t id_to,
    uint32_t player_id,
    const uint8_t* data,
    std::size_t size,
    uint8_t* out,
    std::size_t capacity
) noexcept {
    if (data == nullptr && size != 0)
        return 0;
    Writer w = begin_message(reply_to, command::player_data_changed, out, capacity);
    w.u32(id_to);
    w.u32(player_id);
    w.u32(static_cast<uint32_t>(size));
    w.u32(player_change_body_offset);
    w.bytes(data, size);
    return finish(w);
}

std::size_t encode_player_name_changed(
    const Address& reply_to,
    uint32_t id_to,
    uint32_t player_id,
    const char* short_name,
    const char* long_name,
    uint8_t* out,
    std::size_t capacity
) noexcept {
    Writer w = begin_message(reply_to, command::player_name_changed, out, capacity);
    const std::size_t short_bytes = short_name != nullptr ? utf16z_bytes(short_name) : 0;
    w.u32(id_to);
    w.u32(player_id);
    w.u32(short_name != nullptr ? player_change_body_offset : 0);
    w.u32(
        long_name != nullptr ? static_cast<uint32_t>(player_change_body_offset + short_bytes) : 0
    );
    if (short_name != nullptr)
        w.utf16z(short_name);
    if (long_name != nullptr)
        w.utf16z(long_name);
    return finish(w);
}

uint32_t session_desc_changed_password_offset(const char* name) noexcept {
    return static_cast<uint32_t>(session_desc_changed_name_offset + utf16z_bytes(name));
}

std::size_t encode_session_desc_changed(
    const Address& reply_to,
    uint32_t id_to,
    const SessionDesc& desc,
    const char* name,
    const char* password,
    uint8_t* out,
    std::size_t capacity
) noexcept {
    Writer w = begin_message(reply_to, command::session_desc_changed, out, capacity);
    w.u32(id_to);
    w.u32(session_desc_changed_name_offset);
    w.u32(session_desc_changed_password_offset(name));
    write_desc(w, desc);
    w.utf16z(name);
    w.utf16z(password);
    w.zeros(session_desc_changed_tail_bytes);
    return finish(w);
}

std::size_t encode_super_enum_players_reply(
    const Address& reply_to,
    const SessionDesc& desc,
    const char* name,
    const char* password,
    const PlayerInfo* players,
    std::size_t player_count,
    uint8_t* out,
    std::size_t capacity
) noexcept {
    Writer w = begin_message(reply_to, command::super_enum_players_reply, out, capacity);
    const bool has_password = password != nullptr && password[0] != '\0';
    const std::size_t desc_at = dplay_play_header_bytes + 28;
    const std::size_t name_at = desc_at + session_desc_bytes;
    const std::size_t password_at = name_at + utf16z_bytes(name);
    const std::size_t players_at = password_at + (has_password ? utf16z_bytes(password) : 0);
    w.u32(static_cast<uint32_t>(player_count));
    w.u32(0); // groups
    w.u32(static_cast<uint32_t>(players_at));
    w.u32(0); // shortcuts
    w.u32(static_cast<uint32_t>(desc_at));
    w.u32(static_cast<uint32_t>(name_at));
    w.u32(has_password ? static_cast<uint32_t>(password_at) : 0);
    write_desc(w, desc);
    w.utf16z(name);
    if (has_password)
        w.utf16z(password);
    for (std::size_t i = 0; i < player_count; ++i)
        write_super_packed_player(w, players[i]);
    return finish(w);
}

std::size_t encode_stream_data(
    const Address& reply_to,
    uint32_t from_id,
    uint32_t to_id,
    const uint8_t* payload,
    std::size_t payload_size,
    uint8_t* out,
    std::size_t capacity
) noexcept {
    Writer w{out, capacity, 0, out != nullptr && (payload != nullptr || payload_size == 0)};
    w.u32(0);
    w.sockaddr(reply_to);
    w.u32(from_id);
    w.u32(to_id);
    w.bytes(payload, payload_size);
    return finish(w);
}

std::size_t encode_datagram_data(
    uint32_t from_id,
    uint32_t to_id,
    const uint8_t* payload,
    std::size_t payload_size,
    uint8_t* out,
    std::size_t capacity
) noexcept {
    Writer w{out, capacity, 0, out != nullptr && (payload != nullptr || payload_size == 0)};
    w.u32(from_id);
    w.u32(to_id);
    w.bytes(payload, payload_size);
    return w.ok ? w.at : 0;
}

ParseError decode_stream_data(const uint8_t* bytes, std::size_t size, DataMessage* out) noexcept {
    if (bytes == nullptr || out == nullptr || size < 4)
        return ParseError::truncated;
    if ((load_u32(bytes) >> 20) != dplay_envelope_token)
        return ParseError::not_dplay;
    const std::size_t declared = load_u32(bytes) & dplay_envelope_size_mask;
    if (declared > size || declared < dplay_envelope_bytes + data_ids_bytes)
        return ParseError::truncated;
    if (load_u32(bytes + dplay_envelope_bytes) == dplay_play_signature)
        return ParseError::unsupported;
    out->from_id = load_u32(bytes + dplay_envelope_bytes);
    out->to_id = load_u32(bytes + dplay_envelope_bytes + 4);
    out->payload = bytes + dplay_envelope_bytes + data_ids_bytes;
    out->payload_size = declared - dplay_envelope_bytes - data_ids_bytes;
    return ParseError::ok;
}

} // namespace oa::netgame::dplay
