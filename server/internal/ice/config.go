package ice

import (
	"encoding/json"
	"errors"
	"strings"
)

type Server struct {
	URLs       []string `json:"urls"`
	Username   string   `json:"username,omitempty"`
	Credential string   `json:"credential,omitempty"`
}

func Load(raw string) ([]Server, error) {
	if strings.TrimSpace(raw) == "" {
		return []Server{}, nil
	}
	var servers []Server
	if err := json.Unmarshal([]byte(raw), &servers); err != nil {
		return nil, err
	}
	for _, server := range servers {
		if len(server.URLs) == 0 {
			return nil, errors.New("ICE server URLs must not be empty")
		}
		for _, url := range server.URLs {
			if strings.TrimSpace(url) == "" {
				return nil, errors.New("ICE server URL must not be empty")
			}
		}
	}
	return servers, nil
}
