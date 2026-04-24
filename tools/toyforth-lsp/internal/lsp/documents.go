package lsp

import "toyforth-lsp/internal/analysis"

type document struct {
	URI     string
	Text    string
	Version int
	Index   analysis.DocumentIndex
}

type documentStore struct {
	docs map[string]*document
}

func newDocumentStore() *documentStore {
	return &documentStore{
		docs: make(map[string]*document),
	}
}

func (s *documentStore) open(uri, text string, version int) {
	s.docs[uri] = &document{
		URI:     uri,
		Text:    text,
		Version: version,
		Index:   analysis.IndexDocument(text),
	}
}

func (s *documentStore) update(uri, text string, version int) {
	doc, ok := s.docs[uri]
	if !ok {
		s.open(uri, text, version)
		return
	}

	doc.Text = text
	doc.Version = version
	doc.Index = analysis.IndexDocument(text)
}

func (s *documentStore) close(uri string) {
	delete(s.docs, uri)
}

func (s *documentStore) get(uri string) (*document, bool) {
	doc, ok := s.docs[uri]
	return doc, ok
}
