package lsp

import (
	"bufio"
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"strconv"
	"strings"

	"toyforth-lsp/internal/analysis"
)

type Server struct {
	logger       *log.Logger
	docs         *documentStore
	shutdownSeen bool
}

func NewServer(logger *log.Logger) *Server {
	return &Server{
		logger: logger,
		docs:   newDocumentStore(),
	}
}

func (s *Server) Serve(r io.Reader, w io.Writer) error {
	reader := bufio.NewReader(r)

	for {
		payload, err := readMessage(reader)
		if err != nil {
			if err == io.EOF {
				return nil
			}
			return err
		}

		var req request
		if err := json.Unmarshal(payload, &req); err != nil {
			if err := writeResponse(w, response{
				JSONRPC: "2.0",
				Error: &respError{
					Code:    -32700,
					Message: "parse error",
				},
			}); err != nil {
				return err
			}
			continue
		}

		if err := s.handle(w, req); err != nil {
			return err
		}

		if s.shutdownSeen && req.Method == "exit" {
			return nil
		}
	}
}

func (s *Server) handle(w io.Writer, req request) error {
	switch req.Method {
	case "initialize":
		return s.handleInitialize(w, req)
	case "initialized":
		return nil
	case "shutdown":
		s.shutdownSeen = true
		return writeResponse(w, response{
			JSONRPC: "2.0",
			ID:      decodeID(req.ID),
			Result:  nil,
		})
	case "exit":
		return nil
	case "textDocument/didOpen":
		return s.handleDidOpen(req)
	case "textDocument/didChange":
		return s.handleDidChange(req)
	case "textDocument/didClose":
		return s.handleDidClose(req)
	case "textDocument/documentSymbol":
		return s.handleDocumentSymbol(w, req)
	case "textDocument/definition":
		return s.handleDefinition(w, req)
	case "textDocument/hover":
		return s.handleHover(w, req)
	case "textDocument/references":
		return s.handleReferences(w, req)
	case "textDocument/rename":
		return s.handleRename(w, req)
	default:
		if len(req.ID) == 0 {
			return nil
		}
		return writeResponse(w, response{
			JSONRPC: "2.0",
			ID:      decodeID(req.ID),
			Error: &respError{
				Code:    -32601,
				Message: fmt.Sprintf("method not found: %s", req.Method),
			},
		})
	}
}

func (s *Server) handleInitialize(w io.Writer, req request) error {
	return writeResponse(w, response{
		JSONRPC: "2.0",
		ID:      decodeID(req.ID),
		Result: initializeResult{
			Capabilities: serverCapabilities{
				PositionEncoding:       "utf-16",
				TextDocumentSync:       1,
				DocumentSymbolProvider: true,
				DefinitionProvider:     true,
				HoverProvider:          true,
				ReferencesProvider:     true,
				RenameProvider:         true,
			},
			ServerInfo: serverInfo{
				Name:    "toyforth-lsp",
				Version: "0.1.0",
			},
		},
	})
}

func (s *Server) handleDidOpen(req request) error {
	var params didOpenTextDocumentParams
	if err := json.Unmarshal(req.Params, &params); err != nil {
		return err
	}

	s.docs.open(
		params.TextDocument.URI,
		params.TextDocument.Text,
		params.TextDocument.Version,
	)
	return nil
}

func (s *Server) handleDidChange(req request) error {
	var params didChangeTextDocumentParams
	if err := json.Unmarshal(req.Params, &params); err != nil {
		return err
	}

	if len(params.ContentChanges) == 0 {
		return nil
	}

	s.docs.update(
		params.TextDocument.URI,
		params.ContentChanges[len(params.ContentChanges)-1].Text,
		params.TextDocument.Version,
	)
	return nil
}

func (s *Server) handleDidClose(req request) error {
	var params didCloseTextDocumentParams
	if err := json.Unmarshal(req.Params, &params); err != nil {
		return err
	}

	s.docs.close(params.TextDocument.URI)
	return nil
}

func (s *Server) handleDocumentSymbol(w io.Writer, req request) error {
	var params documentSymbolParams
	if err := json.Unmarshal(req.Params, &params); err != nil {
		return err
	}

	doc, ok := s.docs.get(params.TextDocument.URI)
	if !ok {
		return writeResponse(w, response{
			JSONRPC: "2.0",
			ID:      decodeID(req.ID),
			Result:  []documentSymbol{},
		})
	}

	symbols := make([]documentSymbol, 0, len(doc.Index.Symbols))
	for _, sym := range doc.Index.Symbols {
		rng := lspRange{
			Start: lspPosition{
				Line:      sym.Range.Start.Line,
				Character: sym.Range.Start.Character,
			},
			End: lspPosition{
				Line:      sym.Range.End.Line,
				Character: sym.Range.End.Character,
			},
		}
		selection := lspRange{
			Start: lspPosition{
				Line:      sym.SelectionRange.Start.Line,
				Character: sym.SelectionRange.Start.Character,
			},
			End: lspPosition{
				Line:      sym.SelectionRange.End.Line,
				Character: sym.SelectionRange.End.Character,
			},
		}

		symbols = append(symbols, documentSymbol{
			Name:           sym.Name,
			Detail:         sym.Detail,
			Kind:           sym.Kind,
			Range:          rng,
			SelectionRange: selection,
		})
	}

	return writeResponse(w, response{
		JSONRPC: "2.0",
		ID:      decodeID(req.ID),
		Result:  symbols,
	})
}

func (s *Server) handleDefinition(w io.Writer, req request) error {
	var params definitionParams
	if err := json.Unmarshal(req.Params, &params); err != nil {
		return err
	}

	doc, ok := s.docs.get(params.TextDocument.URI)
	if !ok {
		return writeResponse(w, response{
			JSONRPC: "2.0",
			ID:      decodeID(req.ID),
			Result:  nil,
		})
	}

	sym, ok := analysis.LookupDefinition(doc.Index, analysis.Position{
		Line:      params.Position.Line,
		Character: params.Position.Character,
	})
	if !ok {
		return writeResponse(w, response{
			JSONRPC: "2.0",
			ID:      decodeID(req.ID),
			Result:  nil,
		})
	}

	return writeResponse(w, response{
		JSONRPC: "2.0",
		ID:      decodeID(req.ID),
		Result: location{
			URI: params.TextDocument.URI,
			Range: lspRange{
				Start: lspPosition{
					Line:      sym.SelectionRange.Start.Line,
					Character: sym.SelectionRange.Start.Character,
				},
				End: lspPosition{
					Line:      sym.SelectionRange.End.Line,
					Character: sym.SelectionRange.End.Character,
				},
			},
		},
	})
}

func (s *Server) handleHover(w io.Writer, req request) error {
	var params hoverParams
	if err := json.Unmarshal(req.Params, &params); err != nil {
		return err
	}

	doc, ok := s.docs.get(params.TextDocument.URI)
	if !ok {
		return writeResponse(w, response{
			JSONRPC: "2.0",
			ID:      decodeID(req.ID),
			Result:  nil,
		})
	}

	info, ok := analysis.LookupHover(doc.Index, analysis.Position{
		Line:      params.Position.Line,
		Character: params.Position.Character,
	})
	if !ok {
		return writeResponse(w, response{
			JSONRPC: "2.0",
			ID:      decodeID(req.ID),
			Result:  nil,
		})
	}

	return writeResponse(w, response{
		JSONRPC: "2.0",
		ID:      decodeID(req.ID),
		Result: hover{
			Contents: markupContent{
				Kind:  "markdown",
				Value: info.Contents,
			},
			Range: lspRange{
				Start: lspPosition{
					Line:      info.Range.Start.Line,
					Character: info.Range.Start.Character,
				},
				End: lspPosition{
					Line:      info.Range.End.Line,
					Character: info.Range.End.Character,
				},
			},
		},
	})
}

func (s *Server) handleReferences(w io.Writer, req request) error {
	var params referenceParams
	if err := json.Unmarshal(req.Params, &params); err != nil {
		return err
	}

	doc, ok := s.docs.get(params.TextDocument.URI)
	if !ok {
		return writeResponse(w, response{
			JSONRPC: "2.0",
			ID:      decodeID(req.ID),
			Result:  []location{},
		})
	}

	ranges := analysis.LookupReferences(doc.Index, analysis.Position{
		Line:      params.Position.Line,
		Character: params.Position.Character,
	}, params.Context.IncludeDeclaration)
	locations := make([]location, 0, len(ranges))
	for _, rng := range ranges {
		locations = append(locations, location{
			URI: params.TextDocument.URI,
			Range: lspRange{
				Start: lspPosition{
					Line:      rng.Start.Line,
					Character: rng.Start.Character,
				},
				End: lspPosition{
					Line:      rng.End.Line,
					Character: rng.End.Character,
				},
			},
		})
	}

	return writeResponse(w, response{
		JSONRPC: "2.0",
		ID:      decodeID(req.ID),
		Result:  locations,
	})
}

func (s *Server) handleRename(w io.Writer, req request) error {
	var params renameParams
	if err := json.Unmarshal(req.Params, &params); err != nil {
		return err
	}

	doc, ok := s.docs.get(params.TextDocument.URI)
	if !ok {
		return writeResponse(w, response{
			JSONRPC: "2.0",
			ID:      decodeID(req.ID),
			Result:  nil,
		})
	}

	ranges := analysis.LookupRenameEdits(doc.Index, analysis.Position{
		Line:      params.Position.Line,
		Character: params.Position.Character,
	})
	if len(ranges) == 0 {
		return writeResponse(w, response{
			JSONRPC: "2.0",
			ID:      decodeID(req.ID),
			Result:  nil,
		})
	}

	edits := make([]textEdit, 0, len(ranges))
	for _, rng := range ranges {
		edits = append(edits, textEdit{
			Range: lspRange{
				Start: lspPosition{
					Line:      rng.Start.Line,
					Character: rng.Start.Character,
				},
				End: lspPosition{
					Line:      rng.End.Line,
					Character: rng.End.Character,
				},
			},
			NewText: params.NewName,
		})
	}

	return writeResponse(w, response{
		JSONRPC: "2.0",
		ID:      decodeID(req.ID),
		Result: workspaceEdit{
			Changes: map[string][]textEdit{
				params.TextDocument.URI: edits,
			},
		},
	})
}

func readMessage(r *bufio.Reader) ([]byte, error) {
	contentLength := -1

	for {
		line, err := r.ReadString('\n')
		if err != nil {
			return nil, err
		}

		line = strings.TrimRight(line, "\r\n")
		if line == "" {
			break
		}

		if strings.HasPrefix(strings.ToLower(line), "content-length:") {
			value := strings.TrimSpace(line[len("content-length:"):])
			n, err := strconv.Atoi(value)
			if err != nil {
				return nil, fmt.Errorf("invalid content length %q: %w", value, err)
			}
			contentLength = n
		}
	}

	if contentLength < 0 {
		return nil, fmt.Errorf("missing Content-Length header")
	}

	payload := make([]byte, contentLength)
	if _, err := io.ReadFull(r, payload); err != nil {
		return nil, err
	}
	return payload, nil
}

func writeResponse(w io.Writer, resp response) error {
	payload, err := json.Marshal(resp)
	if err != nil {
		return err
	}

	var buf bytes.Buffer
	fmt.Fprintf(&buf, "Content-Length: %d\r\n\r\n", len(payload))
	buf.Write(payload)
	_, err = w.Write(buf.Bytes())
	return err
}

func decodeID(raw json.RawMessage) any {
	if len(raw) == 0 {
		return nil
	}

	var value any
	if err := json.Unmarshal(raw, &value); err != nil {
		return string(raw)
	}
	return value
}
