// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The automation endpoint: a listener on this machine's loopback address,
// served from the main loop's frame hook on the main thread, never waiting.
//
// One client at a time. A connection whose first bytes are not "AUTO/1 "
// is closed unanswered. Its first frame must be a hello naming the run's
// token, read with limits a hello needs (hello_json_bytes, no payload): a
// connection whose first bytes make no such frame is closed unanswered, a
// wrong token, or another request, is answered denied and the connection
// closed; while a client is connected, another connection's first frame is
// answered busy and the connection closed. A connection that sends no
// frame within first_frame_timeout_ms is closed. The client's requests are
// then taken in order, each answered by the handler the request table
// (requests.hpp) names for its op; a handler may hold its request, to
// answer it on a later frame, and no other request is taken until it does.
// The events the client subscribed to go out unasked among the answers,
// numbered from 1 for each client. Answers and events wait in a buffer
// while the client does not read them; a client that lets it grow past
// max_waiting_output_bytes is dropped. A client that ends its side of the
// connection has every request it sent before answered, and leaves once
// the answers are written. Each time it is served the endpoint reads at
// most read_budget_bytes from each connection and takes requests for at
// most request_budget_us, so that a busy client never holds a frame up;
// and it reads nothing from a connection while a whole frame of its bytes
// waits to be taken, so that a client sending faster than the game answers
// is held back by the connection itself.
#pragma once

#include "oa/app/automation/json.hpp"
#include "oa/app/automation/protocol.hpp"
#include "oa/app/automation_host.hpp"
#include "oa/app/check_host.hpp"
#include "oa/app/extension.hpp"
#include "oa/netgame/stream_socket.hpp"
#include "options.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app {
struct Options;
}

namespace oa::app::automation {

/// Connections the endpoint keeps waiting for their first frame, at most.
inline constexpr size_t max_waiting_connections = 4;
/// How long a connection may take to send its first frame, in milliseconds.
inline constexpr uint64_t first_frame_timeout_ms = 10000;
/// The most bytes read from one connection each time the endpoint is served.
inline constexpr size_t read_budget_bytes = size_t{1} << 20;
/// The most time spent taking requests each time the endpoint is served, in microseconds.
inline constexpr uint64_t request_budget_us = 1000;
/// The largest payload a request may carry, in bytes.
inline constexpr size_t max_request_payload_bytes = size_t{1} << 20;
/// The largest JSON part a connection's first frame may have, in bytes: a
/// hello is far smaller, and its JSON is read before its token is checked.
inline constexpr size_t hello_json_bytes = 4096;
/// The most bytes of answers and events that may wait for a client to read
/// them: one answer of the largest size, and room for others.
inline constexpr size_t max_waiting_output_bytes = max_payload_bytes + 4 * max_json_bytes;
/// The protocol versions the endpoint speaks.
inline constexpr int64_t automation_version = 1;

/// A request being answered.
struct Request {
    int64_t id{};   ///< the id the client gave it
    std::string op; ///< its operation
    Json fields;    ///< the whole request object
};

/// An answer being written.
struct Answer {
    /// The answer's object, open: its id, ok, frame and tick are written,
    /// and the handler adds its own members.
    JsonWriter json;
    std::vector<uint8_t> payload; ///< the payload the answer carries; empty for none
    std::string error_code;       ///< not empty: the request is refused with this code
    std::string error_message;    ///< what the refusal says
    std::string error_field;      ///< the request's field a bad_request names; empty for none
    bool held{};                  ///< the handler answers later (Endpoint::answer_held)

    /// Refuses the request.
    ///
    /// @param code the error's code, such as "bad_request"
    /// @param message what it says
    /// @param field the field it names; empty for none
    void refuse(std::string_view code, std::string_view message, std::string_view field = {});
};

/// The automation endpoint of one process.
class Endpoint {
  public:

    Endpoint() = default;
    Endpoint(const Endpoint&) = delete;
    Endpoint& operator=(const Endpoint&) = delete;

    /// Closes the listener and every connection.
    ~Endpoint();

    /// Starts listening, makes the run's token and writes the address and
    /// token to a file, created with only its owner allowed to read it where
    /// the system has such modes, and replaced at once.
    ///
    /// Throws std::runtime_error when the address cannot be served, no token
    /// can be made or the file cannot be written.
    ///
    /// @param address where to listen
    /// @param file where to write the address and token
    void open(const ListenAddress& address, const std::filesystem::path& file);

    /// Tells whether the endpoint listens.
    ///
    /// @return true once open succeeded
    [[nodiscard]] bool listening() const noexcept;

    /// Serves the endpoint at one stage of a frame: accepts connections,
    /// reads what came, answers requests, writes what waits. Never throws:
    /// a connection that fails is dropped and the game goes on.
    ///
    /// @param[in,out] runtime the running game
    /// @param stage the stage of the frame
    void serve(Runtime& runtime, FrameStage stage) noexcept;

    // What a request handler reads and asks for (requests.hpp). These are
    // valid only while serve runs.

    /// Returns the running game.
    ///
    /// @return the game being served
    [[nodiscard]] Runtime& runtime() const noexcept { return *runtime_; }

    /// Returns the check host bound to the running game.
    ///
    /// @return the table
    [[nodiscard]] const CheckHost& check_host() const noexcept { return check_host_; }

    /// Returns the automation host bound to the running game.
    ///
    /// @return the table
    [[nodiscard]] const AutomationHost& automation_host() const noexcept {
        return automation_host_;
    }

    /// Returns the running game's command-line options.
    ///
    /// @return the options
    [[nodiscard]] const oa::app::Options& options() const noexcept { return *run_options_; }

    /// Returns the frames the endpoint has been served at their pump stage, this one included.
    ///
    /// @return the count
    [[nodiscard]] uint64_t frame() const noexcept { return frame_; }

    /// Returns the running match's tick.
    ///
    /// @return the tick; 0 while no match runs
    [[nodiscard]] uint32_t tick() const noexcept;

    /// Asks the game to quit, as a player closing its window does, once the
    /// answers waiting for the client have been written (take_quit).
    void request_quit() noexcept { quit_asked_ = true; }

    /// Takes the request to quit once the answers waiting for the client have been written.
    ///
    /// @return true once, when the game is to quit now
    [[nodiscard]] bool take_quit() noexcept;

    /// Returns the request a handler holds.
    ///
    /// @return the request, or null when none is held
    [[nodiscard]] const Request* held_request() const noexcept { return held_ ? &*held_ : nullptr; }

    /// Tells whether a client is connected: a connection whose hello was answered.
    ///
    /// @return true while one is
    [[nodiscard]] bool has_client() const noexcept { return client_.has_value(); }

    /// Starts an answer to a request: its id, ok, frame and tick.
    ///
    /// @param id the request's id
    /// @return the answer, its object open for the handler's members
    [[nodiscard]] Answer begin_answer(int64_t id) const;

    /// Sends the answer to the held request, and takes requests again.
    ///
    /// @param[in,out] answer the answer begun for it (begin_answer); its object is closed
    void answer_held(Answer& answer);

    /// Tells whether the client takes events of a kind.
    ///
    /// @param kind the kind's bit (event_kind, events.hpp)
    /// @return true while a client is connected and subscribed to it
    [[nodiscard]] bool subscribed(uint32_t kind) const noexcept {
        return client_ && (subscriptions_ & kind) != 0;
    }

    /// Sets the kinds of event the client is sent, in place of those it took before.
    ///
    /// @param kinds the kinds' bits (event_kind, events.hpp); 0 for none
    void subscribe(uint32_t kinds) noexcept { subscriptions_ = kinds; }

    /// Returns the kinds of event the client is sent.
    ///
    /// @return the kinds' bits; 0 without a client
    [[nodiscard]] uint32_t subscriptions() const noexcept { return client_ ? subscriptions_ : 0; }

    /// Starts an event: its kind, its number among the events sent to the
    /// client (seq, from 1), and the frame and tick it was seen in.
    ///
    /// @param kind the event's kind, as the protocol names it
    /// @return the event, its object open for the event's members
    [[nodiscard]] JsonWriter begin_event(std::string_view kind) const;

    /// Sends an event to the client, after the answers and events queued before it.
    ///
    /// @param[in,out] event the event begun for it (begin_event); its object is closed
    void send_event(JsonWriter& event);

    /// Serves the endpoint between frames, while the game runs none, such as
    /// while a match loads: does `work` with the running game bound, then
    /// writes what waits for the client. Takes no request and accepts no
    /// connection. Never throws: a connection that fails is dropped.
    ///
    /// @param[in,out] runtime the running game
    /// @param work what to do; it may send events
    void
    serve_between_frames(Runtime& runtime, const std::function<void(Endpoint&)>& work) noexcept;

  private:

    // One connection to the endpoint.
    struct Connection {
        intptr_t socket{oa::netgame::sock::invalid_socket};
        /// A hello's limits until the connection is the client's.
        FrameReader reader{hello_json_bytes, 0};
        std::vector<uint8_t> output; ///< bytes waiting to be written
        size_t output_sent{};        ///< of them, those already written
        uint64_t opened_ms{};        ///< the steady clock when it was accepted
        size_t received{};           ///< bytes read so far
        bool unframed{};             ///< its first bytes are not a frame's magic
        bool ended{};                ///< the other end will send nothing more
        bool closing{};              ///< closed once its output is written
    };

    // What writing to a connection came to.
    enum class Written : uint8_t {
        kept_up,  ///< what waits is within max_waiting_output_bytes
        failed,   ///< the connection failed or the other end closed it
        overfull, ///< more than max_waiting_output_bytes wait: the other end does not read
    };

    /// Accepts the connections that wait at the listener.
    void accept_connections();

    /// Reads what came on a connection and gives it to its reader, while the
    /// reader holds less than a whole frame and a read more; marks the
    /// connection ended once the other end will send nothing more.
    ///
    /// @param[in,out] connection the connection
    /// @return false when the connection failed
    bool receive(Connection& connection);

    /// Serves the connections that have not yet sent their first frame.
    void serve_waiting();

    /// Answers a connection's first frame: makes it the client, or refuses it.
    ///
    /// @param[in,out] connection the connection
    /// @param frame its first frame
    void greet(Connection& connection, const Frame& frame);

    /// Takes the client's requests, in order, within the budget.
    void serve_client();

    /// Answers one request of the client's.
    ///
    /// @param frame the request's frame
    void take_request(const Frame& frame);

    /// Queues an answer for a connection, closing its object; a refused one
    /// is written as the refusal alone.
    ///
    /// @param[in,out] connection the connection
    /// @param id the request's id; null when it has none
    /// @param[in,out] answer the answer
    void send(Connection& connection, std::optional<int64_t> id, Answer& answer);

    /// Queues a refusal for a connection.
    ///
    /// @param[in,out] connection the connection
    /// @param id the request's id; null when it has none
    /// @param code the error's code
    /// @param message what it says
    void refuse(
        Connection& connection,
        std::optional<int64_t> id,
        std::string_view code,
        std::string_view message
    );

    /// Writes what waits for a connection, as much as the system takes now.
    ///
    /// @param[in,out] connection the connection
    /// @return what it came to
    Written write(Connection& connection);

    /// Writes what waits for the client, and drops it when its connection
    /// failed, when it does not read, or once it is closing and all is written.
    void write_client();

    /// Drops the client, saying why in the log.
    ///
    /// @param why the line's end: "left", or "dropped: not reading"
    void drop_client(std::string_view why) noexcept;

    /// Closes a connection.
    ///
    /// @param[in,out] connection the connection
    void close(Connection& connection) noexcept;

    intptr_t listener_{oa::netgame::sock::invalid_socket};
    std::string address_; ///< as the file and the log give it
    std::string token_;
    std::vector<Connection> waiting_;
    std::optional<Connection> client_;
    std::optional<Request> held_; ///< the request a handler holds
    uint32_t subscriptions_{};    ///< the kinds of event the client takes (event_kind bits)
    uint64_t events_sent_{};      ///< events sent to the client; the next one's seq is one more
    bool quit_asked_{};
    uint64_t frame_{};
    // Valid only while serve runs.
    Runtime* runtime_{};
    CheckHost check_host_{};
    AutomationHost automation_host_{};
    const oa::app::Options* run_options_{};
};

} // namespace oa::app::automation
