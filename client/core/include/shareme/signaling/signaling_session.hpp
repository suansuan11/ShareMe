#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace shareme::signaling {
enum class Role { none, host, viewer };
struct Envelope { int version{1}; std::string type; std::string room_id; std::uint64_t sequence{}; std::string payload; };
class SignalingSession final {
public:
  [[nodiscard]] Envelope create_room();
  [[nodiscard]] Envelope join_room(std::string room_id);
  [[nodiscard]] std::optional<Envelope> relay(std::string type, std::string payload);
  [[nodiscard]] bool handle(const Envelope& envelope);
  [[nodiscard]] bool joined() const noexcept;
  [[nodiscard]] Role role() const noexcept;
  [[nodiscard]] const std::string& room_id() const noexcept;
  [[nodiscard]] const std::string& token() const noexcept;
private:
  [[nodiscard]] Envelope command(std::string type, std::string room_id, std::string payload);
  Role role_{Role::none}; std::string room_id_; std::string token_; std::uint64_t next_sequence_{1}; bool joined_{false};
};
} // namespace shareme::signaling
