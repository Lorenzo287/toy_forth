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

	"toy-tools/internal/analysis"
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
			Result:  newResult(nil),
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
	case "textDocument/formatting":
		return s.handleFormatting(w, req)
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
	if len(req.Params) > 0 {
		var params initializeParams
		if err := json.Unmarshal(req.Params, &params); err != nil {
			return writeResponse(w, response{
				JSONRPC: "2.0",
				ID:      decodeID(req.ID),
				Error: &respError{
					Code:    -32602,
					Message: fmt.Sprintf("invalid initialize parameters: %v", err),
				},
			})
		}
		for _, err := range s.docs.configureWorkspace(params) {
			s.logger.Printf("workspace indexing: %v", err)
		}
	}

	return writeResponse(w, response{
		JSONRPC: "2.0",
		ID:      decodeID(req.ID),
		Result: newResult(initializeResult{
			Capabilities: serverCapabilities{
				PositionEncoding:           "utf-16",
				TextDocumentSync:           1,
				DocumentSymbolProvider:     true,
				DefinitionProvider:         true,
				HoverProvider:              true,
				ReferencesProvider:         true,
				RenameProvider:             true,
				DocumentFormattingProvider: true,
			},
			ServerInfo: serverInfo{
				Name:    "toyls",
				Version: "0.1.0",
			},
		}),
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
			Result:  newResult([]documentSymbol{}),
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
		Result:  newResult(symbols),
	})
}

func (s *Server) handleDefinition(w io.Writer, req request) error {
	var params definitionParams
	if err := json.Unmarshal(req.Params, &params); err != nil {
		return err
	}

	position := analysis.Position{
		Line:      params.Position.Line,
		Character: params.Position.Character,
	}
	if targetURI, ok := s.resolveSourceReference(params.TextDocument.URI, position); ok {
		return writeResponse(w, response{
			JSONRPC: "2.0",
			ID:      decodeID(req.ID),
			Result: newResult(location{
				URI:   targetURI,
				Range: lspRange{},
			}),
		})
	}

	target, ok := s.resolveDefinition(params.TextDocument.URI, position)
	if !ok {
		return writeResponse(w, response{
			JSONRPC: "2.0",
			ID:      decodeID(req.ID),
			Result:  newResult(nil),
		})
	}

	return writeResponse(w, response{
		JSONRPC: "2.0",
		ID:      decodeID(req.ID),
		Result: newResult(location{
			URI: target.URI,
			Range: lspRange{
				Start: lspPosition{
					Line:      target.Symbol.SelectionRange.Start.Line,
					Character: target.Symbol.SelectionRange.Start.Character,
				},
				End: lspPosition{
					Line:      target.Symbol.SelectionRange.End.Line,
					Character: target.Symbol.SelectionRange.End.Character,
				},
			},
		}),
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
			Result:  newResult(nil),
		})
	}

	position := analysis.Position{
		Line:      params.Position.Line,
		Character: params.Position.Character,
	}
	info, ok := analysis.LookupHover(doc.Index, position)
	if !ok {
		target, resolved := s.resolveDefinition(params.TextDocument.URI, position)
		token, hasToken := analysis.LookupTokenAt(doc.Index, position)
		if resolved && hasToken {
			info = analysis.DefinitionHover(token.Text, target.Symbol, token.Range)
			ok = true
		}
	}
	if !ok {
		info, ok = s.resolveCorePackageHover(params.TextDocument.URI, position)
	}
	if !ok {
		return writeResponse(w, response{
			JSONRPC: "2.0",
			ID:      decodeID(req.ID),
			Result:  newResult(nil),
		})
	}

	return writeResponse(w, response{
		JSONRPC: "2.0",
		ID:      decodeID(req.ID),
		Result: newResult(hover{
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
		}),
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
			Result:  newResult([]location{}),
		})
	}

	position := analysis.Position{
		Line:      params.Position.Line,
		Character: params.Position.Character,
	}
	resolver := newResolutionCache(s.docs)
	target, resolved := s.resolveDefinitionWithResolver(params.TextDocument.URI, position, resolver)
	locations := make([]location, 0)
	if resolved && target.Symbol.Detail == "local binding" {
		for _, rng := range analysis.LookupReferences(doc.Index, position, params.Context.IncludeDeclaration) {
			locations = append(locations, location{URI: doc.URI, Range: toLSPRange(rng)})
		}
	} else if resolved {
		for _, occurrence := range s.wordOccurrencesWithResolver(target, params.Context.IncludeDeclaration, resolver) {
			locations = append(locations, location{URI: occurrence.URI, Range: toLSPRange(occurrence.Range)})
		}
	}

	return writeResponse(w, response{
		JSONRPC: "2.0",
		ID:      decodeID(req.ID),
		Result:  newResult(locations),
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
			Result:  newResult(nil),
		})
	}

	position := analysis.Position{
		Line:      params.Position.Line,
		Character: params.Position.Character,
	}
	resolver := newResolutionCache(s.docs)
	target, resolved := s.resolveDefinitionWithResolver(params.TextDocument.URI, position, resolver)
	if !resolved {
		return writeResponse(w, response{
			JSONRPC: "2.0",
			ID:      decodeID(req.ID),
			Result:  newResult(nil),
		})
	}

	changes := make(map[string][]textEdit)
	if target.Symbol.Detail == "local binding" {
		for _, rng := range analysis.LookupRenameEdits(doc.Index, position) {
			changes[doc.URI] = append(changes[doc.URI], textEdit{
				Range:   toLSPRange(rng),
				NewText: params.NewName,
			})
		}
	} else {
		for _, occurrence := range s.wordOccurrencesWithResolver(target, true, resolver) {
			changes[occurrence.URI] = append(changes[occurrence.URI], textEdit{
				Range:   toLSPRange(occurrence.RenameRange),
				NewText: params.NewName,
			})
		}
	}
	if len(changes) == 0 {
		return writeResponse(w, response{
			JSONRPC: "2.0",
			ID:      decodeID(req.ID),
			Result:  newResult(nil),
		})
	}

	return writeResponse(w, response{
		JSONRPC: "2.0",
		ID:      decodeID(req.ID),
		Result: newResult(workspaceEdit{
			Changes: changes,
		}),
	})
}

func toLSPRange(rng analysis.Range) lspRange {
	return lspRange{
		Start: lspPosition{
			Line:      rng.Start.Line,
			Character: rng.Start.Character,
		},
		End: lspPosition{
			Line:      rng.End.Line,
			Character: rng.End.Character,
		},
	}
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
