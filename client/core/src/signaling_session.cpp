#include "shareme/signaling/signaling_session.hpp"
#include <regex>
namespace shareme::signaling {
namespace { std::optional<std::string> field(const std::string& json, const char* key) { std::smatch match; if (std::regex_search(json, match, std::regex(std::string{"\""}+key+"\":\"([^\"]+)\""))) return match[1]; return std::nullopt; } bool room(const std::string& value) { return std::regex_match(value,std::regex{"[A-Z2-7]{6}"}); } }
Envelope SignalingSession::command(std::string type,std::string room_id,std::string payload){return {1,std::move(type),std::move(room_id),next_sequence_++,std::move(payload)};}
Envelope SignalingSession::create_room(){role_=Role::host;return command("create-room","","{\"role\":\"host\"}");}
Envelope SignalingSession::join_room(std::string id){role_=Role::viewer;return command("join-room","","{\"role\":\"viewer\",\"roomId\":\""+id+"\"}");}
std::optional<Envelope> SignalingSession::relay(std::string type,std::string payload){if(!joined_||(type!="session-description"&&type!="ice-candidate"&&type!="movie-audio-session-description"&&type!="movie-audio-ice-candidate"&&type!="restart-ice"))return std::nullopt;return command(std::move(type),room_id_,std::move(payload));}
bool SignalingSession::handle(const Envelope& e){if(e.version!=1)return false;if(e.type=="room-created"||e.type=="room-joined"){const auto id=field(e.payload,"roomId"), token=field(e.payload,"token");if(!id||!token||!room(*id)||(e.type=="room-created"&&role_!=Role::host)||(e.type=="room-joined"&&role_!=Role::viewer))return false;room_id_=*id;token_=*token;joined_=true;return true;}return joined_&&(e.room_id.empty()||e.room_id==room_id_);}
bool SignalingSession::joined() const noexcept{return joined_;} Role SignalingSession::role() const noexcept{return role_;} const std::string& SignalingSession::room_id() const noexcept{return room_id_;} const std::string& SignalingSession::token() const noexcept{return token_;}
} // namespace shareme::signaling
