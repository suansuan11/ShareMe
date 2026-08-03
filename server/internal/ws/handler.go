package ws

import (
	"context"
	"encoding/json"
	"github.com/suansuan11/ShareMe/server/internal/auth"
	"github.com/suansuan11/ShareMe/server/internal/ice"
	"github.com/suansuan11/ShareMe/server/internal/protocol"
	"github.com/suansuan11/ShareMe/server/internal/room"
	"net/http"
	"nhooyr.io/websocket"
	"strings"
	"sync"
	"time"
)

type client struct {
	conn         *websocket.Conn
	roomID, role string
	mu           sync.Mutex
}
type Handler struct {
	manager *room.Manager
	tokens  *auth.Store
	servers []ice.Server
	mu      sync.Mutex
	clients map[string]map[string]*client
}

func NewHandler(m *room.Manager, servers []ice.Server) *Handler {
	return &Handler{manager: m, tokens: auth.NewStore(time.Now), servers: servers, clients: map[string]map[string]*client{}}
}
func (h *Handler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path == "/healthz" {
		w.Header().Set("Content-Type", "application/json")
		_, _ = w.Write([]byte("{\"status\":\"ok\"}\n"))
		return
	}
	if r.URL.Path != "/v1/ws" {
		http.NotFound(w, r)
		return
	}
	c, err := websocket.Accept(w, r, &websocket.AcceptOptions{InsecureSkipVerify: true})
	if err != nil {
		return
	}
	c.SetReadLimit(protocol.MaxMessageSize)
	cl := &client{conn: c}
	if token := strings.TrimPrefix(r.Header.Get("Authorization"), "Bearer "); token != "" {
		if rec, ok := h.tokens.Lookup(token); ok {
			if _, err := h.manager.Reconnect(rec.RoomID, rec.Role); err == nil {
				cl.roomID, cl.role = rec.RoomID, rec.Role
				h.add(cl)
				h.event(rec.RoomID, opposite(rec.Role), "participant-joined")
			}
		}
	}
	defer func() {
		if cl.roomID != "" {
			_, _ = h.manager.Disconnect(cl.roomID, cl.role)
			h.remove(cl)
			h.event(cl.roomID, opposite(cl.role), "participant-left")
		}
		c.CloseNow()
	}()
	for {
		_, raw, err := c.Read(context.Background())
		if err != nil {
			return
		}
		m, err := protocol.Decode(raw)
		if err != nil {
			h.error(cl, 1, "invalid-message", false)
			continue
		}
		if cl.roomID == "" {
			h.bootstrap(cl, m)
			continue
		}
		if m.RoomID != cl.roomID {
			h.error(cl, m.Sequence, "room-mismatch", false)
			continue
		}
		h.relay(cl, m)
	}
}
func (h *Handler) bootstrap(c *client, m protocol.Message) {
	var p struct{ Role, RoomID string }
	if json.Unmarshal(m.Payload, &p) != nil {
		h.error(c, m.Sequence, "invalid-payload", false)
		return
	}
	var id string
	var err error
	if m.Type == "create-room" && p.Role == room.Host {
		var r room.Room
		r, err = h.manager.CreateHost()
		id = r.ID
	} else if m.Type == "join-room" && p.Role == room.Viewer {
		var r room.Room
		r, err = h.manager.JoinViewer(p.RoomID)
		id = r.ID
	} else {
		h.error(c, m.Sequence, "unauthorized", false)
		return
	}
	if err != nil {
		h.error(c, m.Sequence, "room-unavailable", true)
		return
	}
	token, rec, err := h.tokens.Issue(id, p.Role, 10*time.Minute)
	if err != nil {
		h.error(c, m.Sequence, "server-error", true)
		return
	}
	c.roomID, c.role = id, p.Role
	h.add(c)
	payload, _ := json.Marshal(map[string]any{"roomId": id, "token": token, "tokenExpiresAtMs": rec.ExpiresAt.UnixMilli(), "iceServers": h.servers})
	kind := "room-created"
	if p.Role == room.Viewer {
		kind = "room-joined"
	}
	h.send(c, protocol.Message{Version: 1, Type: kind, Sequence: m.Sequence, Payload: payload})
	if p.Role == room.Viewer {
		h.event(id, room.Host, "participant-joined")
	}
}
func (h *Handler) relay(c *client, m protocol.Message) {
	if m.Type != "session-description" && m.Type != "ice-candidate" &&
		m.Type != "movie-audio-session-description" &&
		m.Type != "movie-audio-ice-candidate" && m.Type != "restart-ice" {
		h.error(c, m.Sequence, "unsupported-message", false)
		return
	}
	h.mu.Lock()
	peer := h.clients[c.roomID][opposite(c.role)]
	h.mu.Unlock()
	if peer == nil {
		h.error(c, m.Sequence, "peer-unavailable", true)
		return
	}
	h.send(peer, m)
}
func (h *Handler) add(c *client) {
	h.mu.Lock()
	if h.clients[c.roomID] == nil {
		h.clients[c.roomID] = map[string]*client{}
	}
	h.clients[c.roomID][c.role] = c
	h.mu.Unlock()
}
func (h *Handler) remove(c *client) {
	h.mu.Lock()
	if group := h.clients[c.roomID]; group != nil {
		delete(group, c.role)
		if len(group) == 0 {
			delete(h.clients, c.roomID)
		}
	}
	h.mu.Unlock()
}
func (h *Handler) event(id, target, kind string) {
	h.mu.Lock()
	c := h.clients[id][target]
	h.mu.Unlock()
	if c != nil {
		payload, _ := json.Marshal(map[string]string{"role": opposite(target)})
		h.send(c, protocol.Message{Version: 1, Type: kind, RoomID: id, Sequence: 1, Payload: payload})
	}
}
func (h *Handler) error(c *client, seq uint64, code string, retry bool) {
	h.send(c, protocol.NewError(seq, code, code, retry, seq))
}
func (h *Handler) send(c *client, m protocol.Message) {
	raw, err := protocol.Encode(m)
	if err != nil {
		return
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	_ = c.conn.Write(context.Background(), websocket.MessageText, raw)
}
func opposite(role string) string {
	if role == room.Host {
		return room.Viewer
	}
	return room.Host
}
