package auth

import (
	"testing"
	"time"
)

func TestIssueAndLookupExpires(t *testing.T) {
	now := time.Unix(100, 0)
	store := NewStore(func() time.Time { return now })

	token, record, err := store.Issue("ABCDEF", "host", time.Minute)
	if err != nil {
		t.Fatalf("Issue() error = %v", err)
	}
	if token == "" || record.RoomID != "ABCDEF" || record.Role != "host" {
		t.Fatalf("Issue() = %q, %#v", token, record)
	}
	if got, ok := store.Lookup(token); !ok || got != record {
		t.Fatalf("Lookup() = %#v, %v", got, ok)
	}

	now = now.Add(time.Minute)
	if _, ok := store.Lookup(token); ok {
		t.Fatal("Lookup() resolved an expired token")
	}
}
