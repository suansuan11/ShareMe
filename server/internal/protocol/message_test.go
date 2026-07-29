package protocol

import "testing"

func TestDecodeAcceptsCreateRoom(t *testing.T) {
	message, err := Decode([]byte(`{"version":1,"type":"create-room","sequence":1,"payload":{"role":"host"}}`))
	if err != nil {
		t.Fatalf("Decode() error = %v", err)
	}
	if message.Type != "create-room" {
		t.Fatalf("message.Type = %q, want create-room", message.Type)
	}
}

func TestDecodeRejectsInvalidEnvelope(t *testing.T) {
	cases := [][]byte{
		[]byte(`{"version":2,"type":"create-room","sequence":1,"payload":{}}`),
		[]byte(`{"version":1,"type":"bad_type","sequence":0,"payload":[]}`),
		[]byte(`{"version":1,"type":"create-room","sequence":1,"payload":{}} {}`),
	}

	for _, raw := range cases {
		if _, err := Decode(raw); err == nil {
			t.Fatalf("Decode(%s) succeeded", raw)
		}
	}
}
