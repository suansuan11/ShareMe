package room

import (
	"crypto/rand"
	"errors"
	"sync"
	"time"
)

type Role = string

const (
	Host   Role = "host"
	Viewer Role = "viewer"
)

var (
	ErrRoomNotFound   = errors.New("room not found")
	ErrViewerOccupied = errors.New("viewer already joined")
)

type Participant struct {
	Active     bool
	GraceUntil time.Time
}
type Room struct {
	ID     string
	Host   Participant
	Viewer Participant
}
type Event struct {
	Type, RoomID string
	Target       Role
}
type Manager struct {
	mu    sync.Mutex
	rooms map[string]Room
	now   func() time.Time
	grace time.Duration
}

func NewManager(_ any, now func() time.Time, grace time.Duration) *Manager {
	if now == nil {
		now = time.Now
	}
	return &Manager{rooms: make(map[string]Room), now: now, grace: grace}
}

func (manager *Manager) CreateHost() (Room, error) {
	manager.mu.Lock()
	defer manager.mu.Unlock()
	for range 8 {
		id, err := randomID()
		if err != nil {
			return Room{}, err
		}
		if _, exists := manager.rooms[id]; exists {
			continue
		}
		room := Room{ID: id, Host: Participant{Active: true}}
		manager.rooms[id] = room
		return room, nil
	}
	return Room{}, errors.New("room ID collision limit reached")
}

func (manager *Manager) JoinViewer(id string) (Room, error) {
	manager.mu.Lock()
	defer manager.mu.Unlock()
	room, ok := manager.rooms[id]
	if !ok {
		return Room{}, ErrRoomNotFound
	}
	if room.Viewer.Active || !room.Viewer.GraceUntil.IsZero() {
		return Room{}, ErrViewerOccupied
	}
	room.Viewer.Active = true
	manager.rooms[id] = room
	return room, nil
}

func (manager *Manager) Disconnect(id string, role Role) ([]Event, error) {
	manager.mu.Lock()
	defer manager.mu.Unlock()
	room, ok := manager.rooms[id]
	if !ok {
		return nil, ErrRoomNotFound
	}
	p := participant(&room, role)
	p.Active = false
	p.GraceUntil = manager.now().Add(manager.grace)
	manager.rooms[id] = room
	return []Event{{Type: "participant-left", RoomID: id, Target: opposite(role)}}, nil
}

func (manager *Manager) Reconnect(id string, role Role) ([]Event, error) {
	manager.mu.Lock()
	defer manager.mu.Unlock()
	room, ok := manager.rooms[id]
	if !ok {
		return nil, ErrRoomNotFound
	}
	p := participant(&room, role)
	if p.GraceUntil.IsZero() || !manager.now().Before(p.GraceUntil) {
		return nil, ErrRoomNotFound
	}
	p.Active = true
	p.GraceUntil = time.Time{}
	manager.rooms[id] = room
	return []Event{{Type: "participant-joined", RoomID: id, Target: opposite(role)}}, nil
}

func (manager *Manager) Cleanup() []Event {
	manager.mu.Lock()
	defer manager.mu.Unlock()
	var events []Event
	for id, room := range manager.rooms {
		if expired(room.Host, manager.now()) {
			delete(manager.rooms, id)
			events = append(events, Event{Type: "room-closed", RoomID: id, Target: Viewer})
			continue
		}
		if expired(room.Viewer, manager.now()) {
			room.Viewer = Participant{}
			manager.rooms[id] = room
		}
	}
	return events
}

func (manager *Manager) Get(id string) (Room, bool) {
	manager.mu.Lock()
	defer manager.mu.Unlock()
	room, ok := manager.rooms[id]
	return room, ok
}
func participant(room *Room, role Role) *Participant {
	if role == Host {
		return &room.Host
	}
	return &room.Viewer
}
func opposite(role Role) Role {
	if role == Host {
		return Viewer
	}
	return Host
}
func expired(p Participant, now time.Time) bool {
	return !p.Active && !p.GraceUntil.IsZero() && !now.Before(p.GraceUntil)
}
func randomID() (string, error) {
	const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"
	raw := make([]byte, 6)
	if _, err := rand.Read(raw); err != nil {
		return "", err
	}
	for i := range raw {
		raw[i] = alphabet[int(raw[i])%len(alphabet)]
	}
	return string(raw), nil
}
