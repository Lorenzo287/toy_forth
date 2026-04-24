package parser

import (
	tree_sitter "github.com/tree-sitter/go-tree-sitter"

	"toyforth-lsp/internal/parser/toyforth"
)

func Parse(src []byte) (*tree_sitter.Tree, error) {
	p := tree_sitter.NewParser()
	defer p.Close()

	if err := p.SetLanguage(toyforth.Language()); err != nil {
		return nil, err
	}

	return p.Parse(src, nil), nil
}
