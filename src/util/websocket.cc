#include "websocket.h"

#include "base64.h"
#include "sha1.h"

#include <iostream>
#include <sstream>

namespace util {
namespace {

constexpr char websocket_id[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

struct websocket_handler : http_handler {
  websocket_handler(http_method method, uri target,
                    std::function<future<status>(websocket&)> handler) noexcept
      : target(std::move(target)),
        handler(std::move(handler)),
        has_get(method == http_method::get) {}

  status header(case_insensitive_string_view name,
                std::string_view value) noexcept override {
    // TODO: Make the matching here more strict. This will match many incorrect
    // values, such as `Connection: UpgradePotato` or
    // `Sec-WebSocket-Version: 913`.
    if (name == "Connection" &&
        case_insensitive_string_view(value.data(), value.size())
                .find("Upgrade") != case_insensitive_string_view::npos) {
      has_connection_upgrade = true;
    } else if (name == "Upgrade" && value == "websocket") {
      has_upgrade_websocket = true;
    } else if (name == "Sec-WebSocket-Key") {
      key = std::string(value);
    } else if (name == "Sec-WebSocket-Version" &&
               value.find("13") != value.npos) {
      has_compatible_version = true;
    }
    return status_code::ok;
  }

  future<status> run(tcp::stream& client) noexcept override {
    const bool valid = has_get && has_connection_upgrade &&
                       has_upgrade_websocket && has_compatible_version &&
                       !key.empty();
    if (!valid) {
      std::string_view response =
          "HTTP/1.1 400 Bad Upgrade\r\n"
          "Content-Type: text/plain\r\n"
          "Content-Length: 21\r\n"
          "\r\n"
          "Bad WebSocket Upgrade";
      co_return co_await client.write(response);
    }
    // Compute the acceptance key:
    char buffer[128];
    const std::string_view accept_key =
        base64_encode(sha1(key + websocket_id).bytes, buffer);
    std::ostringstream response_stream;
    response_stream << "HTTP/1.1 101 Switching Protocols\r\n"
                       "Upgrade: websocket\r\n"
                       "Connection: Upgrade\r\n"
                       "Sec-WebSocket-Accept: "
                    << accept_key
                    << "\r\n"
                       "\r\n";
    std::string response = response_stream.str();
    if (status s = co_await client.write(response); s.failure()) {
      co_return error{std::move(s)};
    }
    // WebSocket is established.
    websocket w{client};
    co_return co_await handler(w);
  }

  const uri target;
  std::function<future<status>(websocket&)> handler;
  bool has_get = false;
  bool has_connection_upgrade = false;
  bool has_upgrade_websocket = false;
  bool has_compatible_version = false;
  std::string key;
};

// WebSocket frame layout diagram, taken from RFC 6455:
//
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-------+-+-------------+-------------------------------+
// |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
// |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
// |N|V|V|V|       |S|             |   (if payload len==126/127)   |
// | |1|2|3|       |K|             |                               |
// +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
// |     Extended payload length continued, if payload len == 127  |
// + - - - - - - - - - - - - - - - +-------------------------------+
// |                               |Masking-key, if MASK set to 1  |
// +-------------------------------+-------------------------------+
// | Masking-key (continued)       |          Payload Data         |
// +-------------------------------- - - - - - - - - - - - - - - - +
// :                     Payload Data continued ...                :
// + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
// |                     Payload Data continued ...                |
// +---------------------------------------------------------------+

struct frame_header {
  unsigned char fin : 1;
  unsigned char rsv : 3;
  websocket::frame_type opcode : 4;
  bool mask : 1;
  std::uint64_t payload_length;
  char masking_key[4];
};

future<result<frame_header>> read_frame_header(tcp::stream& socket) noexcept {
  char buffer[14] = {};
  if (status s = co_await socket.read(span<char>(buffer).subspan(0, 6));
      s.failure()) {
    co_return error{std::move(s)};
  }
  const unsigned char prefix1 = buffer[0], prefix2 = buffer[1];
  frame_header output;
  output.fin = prefix1 >> 7;
  output.rsv = (prefix1 >> 4) & 7;
  output.opcode = websocket::frame_type{(unsigned char)(prefix1 & 0xF)};
  output.mask = prefix2 >> 7;
  output.payload_length = prefix2 & 0x7F;
  if (output.payload_length == 126) {
    // 2 byte payload length.
    if (status s = co_await socket.read(span<char>(buffer).subspan(6, 2));
        s.failure()) {
      co_return error{std::move(s)};
    }
    output.payload_length =
        (std::uint64_t)buffer[2] << 8 | (std::uint64_t)buffer[3];
    output.masking_key[0] = buffer[4];
    output.masking_key[1] = buffer[5];
    output.masking_key[2] = buffer[6];
    output.masking_key[3] = buffer[7];
  } else if (output.payload_length == 127) {
    // 8 byte payload length.
    if (status s = co_await socket.read(span<char>(buffer).subspan(6, 8));
        s.failure()) {
      co_return error{std::move(s)};
    }
    output.payload_length =
        (std::uint64_t)buffer[2] << 56 | (std::uint64_t)buffer[3] << 48 |
        (std::uint64_t)buffer[4] << 40 | (std::uint64_t)buffer[5] << 32 |
        (std::uint64_t)buffer[6] << 24 | (std::uint64_t)buffer[7] << 16 |
        (std::uint64_t)buffer[8] << 8 | (std::uint64_t)buffer[9];
    output.masking_key[0] = buffer[10];
    output.masking_key[1] = buffer[11];
    output.masking_key[2] = buffer[12];
    output.masking_key[3] = buffer[13];
  } else {
    // Embedded payload length.
    output.masking_key[0] = buffer[2];
    output.masking_key[1] = buffer[3];
    output.masking_key[2] = buffer[4];
    output.masking_key[3] = buffer[5];
  }
  co_return output;
}

}  // namespace

websocket::websocket(tcp::stream& socket) noexcept : socket_(&socket) {}

future<result<websocket::message>> websocket::receive_message(
    span<char> buffer) noexcept {
  result<frame_header> header = co_await read_frame_header(*socket_);
  if (header.failure()) {
    co_return error{std::move(header).status()};
  }
  if (header->rsv) {
    co_return error{status(status_code::client_error, "rsv is nonzero")};
  }
  if (!header->mask) {
    co_return error{
        status(status_code::client_error, "client frames must be masked")};
  }
  if (header->payload_length > buffer.size()) {
    co_return error{status_code::exhausted};
  }
  const span<char> output = buffer.subspan(0, header->payload_length);
  if (status s = co_await socket_->read(output); s.failure()) {
    co_return error{std::move(s)};
  }
  // Unmask the payload.
  for (int i = 0, n = output.size(); i < n; i++) {
    output[i] ^= header->masking_key[i % 4];
  }
  co_return message{.type = header->opcode, .payload = output};
}

future<status> websocket::send_message(message message) noexcept {
  char buffer[10] = {};
  buffer[0] = (unsigned char)(0x80 | (int)message.type);
  int header_size;
  if (message.payload.size() < 126) {
    // Embedded payload length.
    buffer[1] = message.payload.size();
    header_size = 2;
  } else if (message.payload.size() < 65536) {
    // 2 byte payload length.
    buffer[1] = 126;
    buffer[2] = (unsigned char)(message.payload.size() >> 8);
    buffer[3] = (unsigned char)message.payload.size();
    header_size = 4;
  } else {
    // 8 byte payload length.
    buffer[1] = 127;
    buffer[2] = (unsigned char)(message.payload.size() >> 56);
    buffer[3] = (unsigned char)(message.payload.size() >> 48);
    buffer[4] = (unsigned char)(message.payload.size() >> 40);
    buffer[5] = (unsigned char)(message.payload.size() >> 32);
    buffer[6] = (unsigned char)(message.payload.size() >> 24);
    buffer[7] = (unsigned char)(message.payload.size() >> 16);
    buffer[8] = (unsigned char)(message.payload.size() >> 8);
    buffer[9] = (unsigned char)message.payload.size();
    header_size = 10;
  }
  span<const char> header = span<const char>(buffer).subspan(0, header_size);
  if (status s = co_await socket_->write(header); s.failure()) {
    error{std::move(s)};
  }
  co_return co_await socket_->write(message.payload);
}

std::ostream& operator<<(std::ostream& output,
                         websocket::frame_type t) noexcept {
  switch (t) {
    case websocket::frame_type::continuation:
      return output << "continuation";
    case websocket::frame_type::text:
      return output << "text";
    case websocket::frame_type::binary:
      return output << "binary";
    case websocket::frame_type::close:
      return output << "close";
    case websocket::frame_type::ping:
      return output << "ping";
    case websocket::frame_type::pong:
      return output << "pong";
    default:
      return output << "<unknown>";
  }
}

handle_websocket::handle_websocket(
    std::function<future<status>(websocket&)> handler) noexcept
    : handler_(handler) {}

std::unique_ptr<http_handler> handle_websocket::operator()(
    http_method method, uri target) noexcept {
  return std::make_unique<websocket_handler>(method, std::move(target),
                                             handler_);
}

}  // namespace util
