#pragma once

#include <dwe/game/coop/link.hpp>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace dwe::app {

struct CoopConnectionDesc {
  game::coop::Identity identity;
  std::string configurationError;
  std::function<std::unique_ptr<net::PeerLink>()> host;
  std::function<std::unique_ptr<net::PeerLink>(std::string_view)> join;
};

// App-level host/join handshake. It owns the transport and CoopLink for as long as the lobby is
// open, and keeps the room code and failure state available to the UI.
class CoopConnection {
public:
  enum class View { Closed, Menu, JoinEntry, Hosting, Joining, Connected, Error };

  explicit CoopConnection(CoopConnectionDesc desc) : desc_{std::move(desc)} {}
  void open();
  void close();
  [[nodiscard]] bool host();
  void beginJoin();
  bool submitJoin();
  void type(std::string_view text, bool enter);
  void tick(net::Millis now);

  [[nodiscard]] View view() const { return view_; }
  [[nodiscard]] bool takesText() const { return view_ == View::JoinEntry; }
  [[nodiscard]] bool connected() const { return view_ == View::Connected; }
  [[nodiscard]] std::string code() const;
  [[nodiscard]] const std::string& typed() const { return typed_; }
  [[nodiscard]] std::string_view error() const { return error_; }
  [[nodiscard]] game::coop::CoopLink* link() { return link_.get(); }
  [[nodiscard]] const game::coop::CoopLink* link() const { return link_.get(); }

private:
  bool join(std::string_view code);
  void refresh();

  CoopConnectionDesc desc_;
  std::unique_ptr<net::PeerLink> peer_;
  std::unique_ptr<game::coop::CoopLink> link_;
  View view_ = View::Closed;
  std::string typed_;
  std::string error_;
};

[[nodiscard]] std::string_view coopConnectionViewName(CoopConnection::View view);

} // namespace dwe::app
