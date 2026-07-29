package ice

import "testing"

func TestLoadAcceptsEmptyAndValidConfiguration(t *testing.T) {
	empty, err := Load("")
	if err != nil || len(empty) != 0 {
		t.Fatalf("Load(empty) = %#v, %v", empty, err)
	}

	servers, err := Load(`[{"urls":["stun:example.test:3478"]}]`)
	if err != nil || len(servers) != 1 || servers[0].URLs[0] != "stun:example.test:3478" {
		t.Fatalf("Load(valid) = %#v, %v", servers, err)
	}
}

func TestLoadRejectsServerWithoutURLs(t *testing.T) {
	if _, err := Load(`[{"urls":[]}]`); err == nil {
		t.Fatal("Load() accepted an empty URL list")
	}
}
