package auth

import (
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"sync"
	"time"
)

type Record struct {
	RoomID    string
	Role      string
	ExpiresAt time.Time
}

type Store struct {
	mu      sync.Mutex
	now     func() time.Time
	records map[[sha256.Size]byte]Record
}

func NewStore(now func() time.Time) *Store {
	if now == nil {
		now = time.Now
	}
	return &Store{now: now, records: make(map[[sha256.Size]byte]Record)}
}

func (store *Store) Issue(roomID, role string, ttl time.Duration) (string, Record, error) {
	raw := make([]byte, 32)
	if _, err := rand.Read(raw); err != nil {
		return "", Record{}, err
	}
	token := base64.RawURLEncoding.EncodeToString(raw)
	record := Record{RoomID: roomID, Role: role, ExpiresAt: store.now().Add(ttl)}

	store.mu.Lock()
	store.records[sha256.Sum256([]byte(token))] = record
	store.mu.Unlock()
	return token, record, nil
}

func (store *Store) Lookup(token string) (Record, bool) {
	digest := sha256.Sum256([]byte(token))
	store.mu.Lock()
	defer store.mu.Unlock()
	record, ok := store.records[digest]
	if !ok || !store.now().Before(record.ExpiresAt) {
		delete(store.records, digest)
		return Record{}, false
	}
	return record, true
}

func (store *Store) Revoke(token string) {
	store.mu.Lock()
	delete(store.records, sha256.Sum256([]byte(token)))
	store.mu.Unlock()
}
