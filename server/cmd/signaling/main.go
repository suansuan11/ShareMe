package main

import (
	"context"
	"github.com/suansuan11/ShareMe/server/internal/ice"
	"github.com/suansuan11/ShareMe/server/internal/room"
	"github.com/suansuan11/ShareMe/server/internal/ws"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"
)

func main() {
	servers, err := ice.Load(os.Getenv("SHAREME_ICE_SERVERS_JSON"))
	if err != nil {
		log.Fatal(err)
	}
	addr := os.Getenv("SHAREME_SIGNALING_ADDR")
	if addr == "" {
		addr = "127.0.0.1:8080"
	}
	server := &http.Server{Addr: addr, Handler: ws.NewHandler(room.NewManager(nil, time.Now, 30*time.Second), servers)}
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	go func() { <-ctx.Done(); _ = server.Shutdown(context.Background()) }()
	log.Printf("signaling listening on %s", addr)
	if err := server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
		log.Fatal(err)
	}
}
