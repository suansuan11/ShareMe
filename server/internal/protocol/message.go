package protocol

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"regexp"
)

const (
	Version        = 1
	MaxMessageSize = 64 * 1024
)

var (
	typePattern   = regexp.MustCompile(`^[a-z][a-z0-9]*(?:-[a-z0-9]+)*$`)
	roomIDPattern = regexp.MustCompile(`^[A-Z2-7]{6}$`)
)

// Message is the version-1 envelope shared by signaling and data-channel
// messages. Payload remains type-specific JSON owned by the caller.
type Message struct {
	Version  int             `json:"version"`
	Type     string          `json:"type"`
	RoomID   string          `json:"roomId,omitempty"`
	Sequence uint64          `json:"sequence"`
	Payload  json.RawMessage `json:"payload"`
}

// ErrorPayload is returned in a type=error message for application failures.
type ErrorPayload struct {
	Code            string `json:"code"`
	Message         string `json:"message"`
	Retryable       bool   `json:"retryable"`
	RelatedSequence uint64 `json:"relatedSequence,omitempty"`
}

func Decode(raw []byte) (Message, error) {
	if len(raw) == 0 || len(raw) > MaxMessageSize {
		return Message{}, errors.New("message size is invalid")
	}

	decoder := json.NewDecoder(bytes.NewReader(raw))
	decoder.DisallowUnknownFields()
	var message Message
	if err := decoder.Decode(&message); err != nil {
		return Message{}, fmt.Errorf("decode envelope: %w", err)
	}
	if decoder.More() {
		return Message{}, errors.New("message has trailing JSON")
	}
	if message.Version != Version {
		return Message{}, errors.New("unsupported protocol version")
	}
	if len(message.Type) == 0 || len(message.Type) > 48 || !typePattern.MatchString(message.Type) {
		return Message{}, errors.New("message type is invalid")
	}
	if message.RoomID != "" && !roomIDPattern.MatchString(message.RoomID) {
		return Message{}, errors.New("room ID is invalid")
	}
	if message.Sequence == 0 {
		return Message{}, errors.New("message sequence is invalid")
	}
	if len(message.Payload) == 0 || !json.Valid(message.Payload) {
		return Message{}, errors.New("message payload is invalid")
	}
	var payloadObject map[string]json.RawMessage
	if err := json.Unmarshal(message.Payload, &payloadObject); err != nil || payloadObject == nil {
		return Message{}, errors.New("message payload must be an object")
	}
	return message, nil
}

func Encode(message Message) ([]byte, error) {
	if _, err := Decode(mustMarshal(message)); err != nil {
		return nil, err
	}
	return json.Marshal(message)
}

func NewError(sequence uint64, code, message string, retryable bool, relatedSequence uint64) Message {
	payload, _ := json.Marshal(ErrorPayload{
		Code:            code,
		Message:         message,
		Retryable:       retryable,
		RelatedSequence: relatedSequence,
	})
	return Message{
		Version:  Version,
		Type:     "error",
		Sequence: sequence,
		Payload:  payload,
	}
}

func mustMarshal(message Message) []byte {
	raw, err := json.Marshal(message)
	if err != nil {
		return nil
	}
	return raw
}
