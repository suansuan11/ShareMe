package room

import (
	"errors"
	"testing"
	"time"
)

func TestSecondViewerIsRejected(t *testing.T) {
	manager := NewManager(nil, time.Now, 30*time.Second)
	created, err := manager.CreateHost()
	if err != nil {
		t.Fatal(err)
	}
	if _, err := manager.JoinViewer(created.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := manager.JoinViewer(created.ID); !errors.Is(err, ErrViewerOccupied) {
		t.Fatalf("JoinViewer() error = %v", err)
	}
}

func TestHostGraceExpiryClosesRoom(t *testing.T) {
	now := time.Unix(100, 0)
	manager := NewManager(nil, func() time.Time { return now }, 30*time.Second)
	created, err := manager.CreateHost()
	if err != nil {
		t.Fatal(err)
	}
	if _, err := manager.JoinViewer(created.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := manager.Disconnect(created.ID, Host); err != nil {
		t.Fatal(err)
	}
	now = now.Add(31 * time.Second)
	events := manager.Cleanup()
	if len(events) != 1 || events[0].Type != "room-closed" {
		t.Fatalf("Cleanup() = %#v", events)
	}
	if _, ok := manager.Get(created.ID); ok {
		t.Fatal("room remains after host grace")
	}
}
