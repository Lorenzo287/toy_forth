package main

import (
	"log"
	"os"

	"toyforth-lsp/internal/lsp"
)

func main() {
	logger := log.New(os.Stderr, "toyforth-lsp: ", log.LstdFlags|log.Lshortfile)
	server := lsp.NewServer(logger)
	if err := server.Serve(os.Stdin, os.Stdout); err != nil {
		logger.Fatal(err)
	}
}
