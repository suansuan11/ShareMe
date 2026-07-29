package ws

import (
	"context"
	"encoding/json"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/suansuan11/ShareMe/server/internal/protocol"
	"github.com/suansuan11/ShareMe/server/internal/room"
	"nhooyr.io/websocket"
)

func TestHealthCreateJoinAndRelay(t *testing.T) {
	h := NewHandler(room.NewManager(nil, time.Now, 30*time.Second), nil)
	s := httptest.NewServer(h)
	defer s.Close()
	if response, err := s.Client().Get(s.URL + "/healthz"); err != nil || response.StatusCode != 200 {
		t.Fatalf("health = %v, %v", response, err)
	}
	ctx := context.Background()
	url := "ws" + strings.TrimPrefix(s.URL, "http") + "/v1/ws"
	host, _, err := websocket.Dial(ctx, url, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer host.CloseNow()
	write(t, host, message("create-room", "", 1, map[string]any{"role": "host"}))
	created := read(t, host)
	roomID := stringField(t, created, "roomId")
	viewer, _, err := websocket.Dial(ctx, url, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer viewer.CloseNow()
	write(t, viewer, message("join-room", "", 1, map[string]any{"role": "viewer", "roomId": roomID}))
	if got := read(t, viewer); got.Type != "room-joined" {
		t.Fatalf("join type = %s", got.Type)
	}
	if got := read(t, host); got.Type != "participant-joined" {
		t.Fatalf("event type = %s", got.Type)
	}
	write(t, host, message("session-description", roomID, 2, map[string]any{"descriptionType": "offer", "sdp": "opaque"}))
	if got := read(t, viewer); got.Type != "session-description" {
		t.Fatalf("relay type = %s", got.Type)
	}
}
func message(kind, roomID string, sequence uint64, payload any) protocol.Message {
	raw, _ := json.Marshal(payload)
	return protocol.Message{Version: 1, Type: kind, RoomID: roomID, Sequence: sequence, Payload: raw}
}
func write(t *testing.T, c *websocket.Conn, m protocol.Message) {
	t.Helper()
	raw, _ := protocol.Encode(m)
	if err := c.Write(context.Background(), websocket.MessageText, raw); err != nil {
		t.Fatal(err)
	}
}
func read(t *testing.T, c *websocket.Conn) protocol.Message {
	t.Helper()
	_, raw, err := c.Read(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	m, err := protocol.Decode(raw)
	if err != nil {
		t.Fatal(err)
	}
	return m
}
func stringField(t *testing.T, m protocol.Message, key string) string {
	t.Helper()
	var p map[string]any
	_ = json.Unmarshal(m.Payload, &p)
	value, _ := p[key].(string)
	if value == "" {
		t.Fatalf("%s missing in %s", key, m.Payload)
	}
	return value
}
