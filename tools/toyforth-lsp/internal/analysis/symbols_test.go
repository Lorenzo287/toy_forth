package analysis

import (
	"os"
	"path/filepath"
	"testing"
)

func TestDocumentSymbols(t *testing.T) {
	path := filepath.Join("..", "..", "testdata", "symbols.fth")
	src, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read fixture: %v", err)
	}

	symbols := DocumentSymbols(string(src))
	if len(symbols) != 3 {
		t.Fatalf("expected 3 symbols, got %d", len(symbols))
	}

	if symbols[0].Name != "square" || symbols[0].Detail != "colon definition" {
		t.Fatalf("unexpected first symbol: %+v", symbols[0])
	}
	if symbols[0].Doc != "top-level definitions" {
		t.Fatalf("unexpected square doc: %+v", symbols[0])
	}
	if symbols[0].StackEffect != "n -- n*n" {
		t.Fatalf("unexpected square stack effect: %+v", symbols[0])
	}
	if symbols[0].SelectionRange.Start.Character != 2 {
		t.Fatalf("unexpected square selection range: %+v", symbols[0].SelectionRange)
	}

	if symbols[1].Name != "cube" || symbols[1].Detail != "quoted symbol def" {
		t.Fatalf("unexpected second symbol: %+v", symbols[1])
	}
	if symbols[1].Doc != "definition comment" {
		t.Fatalf("unexpected cube doc: %+v", symbols[1])
	}
	if symbols[1].SelectionRange.Start.Character != 1 {
		t.Fatalf("unexpected cube selection range: %+v", symbols[1].SelectionRange)
	}

	if symbols[2].Name != "shadow-demo" || symbols[2].Detail != "colon definition" {
		t.Fatalf("unexpected third symbol: %+v", symbols[2])
	}
}

func TestLookupDefinition(t *testing.T) {
	path := filepath.Join("..", "..", "testdata", "symbols.fth")
	src, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read fixture: %v", err)
	}

	index := IndexDocument(string(src))

	tests := []struct {
		name string
		pos  Position
		want string
	}{
		{
			name: "call to square resolves",
			pos: Position{
				Line:      6,
				Character: 2,
			},
			want: "square",
		},
		{
			name: "call to cube resolves",
			pos: Position{
				Line:      7,
				Character: 2,
			},
			want: "cube",
		},
		{
			name: "local fetch resolves to local binding",
			pos: Position{
				Line:      1,
				Character: 28,
			},
			want: "n",
		},
		{
			name: "nested local fetch resolves to inner binding",
			pos: Position{
				Line:      9,
				Character: 43,
			},
			want: "outer",
		},
		{
			name: "post-block local fetch resolves to outer binding",
			pos: Position{
				Line:      9,
				Character: 57,
			},
			want: "outer",
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			sym, ok := LookupDefinition(index, tc.pos)
			if !ok {
				t.Fatalf("expected definition at %+v", tc.pos)
			}
			if sym.Name != tc.want {
				t.Fatalf("expected %q, got %+v", tc.want, sym)
			}
			if sym.SelectionRange.Start.Character >= sym.Range.Start.Character && sym.Name == "square" && sym.SelectionRange.Start.Character != 2 {
				t.Fatalf("unexpected square target range: %+v", sym)
			}
			if tc.name == "local fetch resolves to local binding" && sym.SelectionRange.Start.Character != 24 {
				t.Fatalf("unexpected local binding target: %+v", sym)
			}
			if tc.name == "nested local fetch resolves to inner binding" && sym.SelectionRange.Start.Character != 35 {
				t.Fatalf("unexpected inner local target: %+v", sym)
			}
			if tc.name == "post-block local fetch resolves to outer binding" && sym.SelectionRange.Start.Character != 16 {
				t.Fatalf("unexpected outer local target: %+v", sym)
			}
		})
	}
}

func TestLookupHover(t *testing.T) {
	path := filepath.Join("..", "..", "testdata", "symbols.fth")
	src, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read fixture: %v", err)
	}

	index := IndexDocument(string(src))

	userHover, ok := LookupHover(index, Position{Line: 7, Character: 2})
	if !ok {
		t.Fatalf("expected hover for user word")
	}
	if userHover.Contents != "```toyforth\ncube\n```\ndefinition comment" {
		t.Fatalf("unexpected user hover: %q", userHover.Contents)
	}

	stackEffectHover, ok := LookupHover(index, Position{Line: 6, Character: 2})
	if !ok {
		t.Fatalf("expected hover for colon definition call")
	}
	if stackEffectHover.Contents != "```toyforth\nsquare ( n -- n*n )\n```\ntop-level definitions\n\nStack effect: `n -- n*n`" {
		t.Fatalf("unexpected stack effect hover: %q", stackEffectHover.Contents)
	}

	localHover, ok := LookupHover(index, Position{Line: 9, Character: 43})
	if !ok {
		t.Fatalf("expected hover for local binding")
	}
	if localHover.Contents != "```toyforth\n$outer\n```\nLocal binding from `{ outer }`." {
		t.Fatalf("unexpected local hover: %q", localHover.Contents)
	}

	builtinHover, ok := LookupHover(index, Position{Line: 15, Character: 8})
	if !ok {
		t.Fatalf("expected hover for builtin word")
	}
	if builtinHover.Range.Start.Character != 7 {
		t.Fatalf("unexpected builtin hover range: %+v", builtinHover.Range)
	}
	if builtinHover.Contents == "" {
		t.Fatalf("expected builtin hover contents")
	}
}

func TestLookupReferences(t *testing.T) {
	path := filepath.Join("..", "..", "testdata", "symbols.fth")
	src, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read fixture: %v", err)
	}

	index := IndexDocument(string(src))

	wordRefs := LookupReferences(index, Position{Line: 6, Character: 2}, true)
	if len(wordRefs) != 2 {
		t.Fatalf("expected 2 references for square including declaration, got %d", len(wordRefs))
	}
	if wordRefs[0].Start.Line != 1 || wordRefs[0].Start.Character != 2 {
		t.Fatalf("unexpected square declaration ref: %+v", wordRefs[0])
	}
	if wordRefs[1].Start.Line != 6 || wordRefs[1].Start.Character != 2 {
		t.Fatalf("unexpected square call ref: %+v", wordRefs[1])
	}

	localRefs := LookupReferences(index, Position{Line: 9, Character: 43}, true)
	if len(localRefs) != 2 {
		t.Fatalf("expected 2 references for inner outer including declaration, got %d", len(localRefs))
	}
	if localRefs[0].Start.Line != 9 || localRefs[0].Start.Character != 35 {
		t.Fatalf("unexpected inner local declaration ref: %+v", localRefs[0])
	}
	if localRefs[1].Start.Line != 9 || localRefs[1].Start.Character != 43 {
		t.Fatalf("unexpected inner local use ref: %+v", localRefs[1])
	}

	localDeclRefs := LookupReferences(index, Position{Line: 9, Character: 35}, true)
	if len(localDeclRefs) != 2 {
		t.Fatalf("expected 2 references when invoked on inner local declaration, got %d", len(localDeclRefs))
	}
	if localDeclRefs[0].Start.Line != 9 || localDeclRefs[0].Start.Character != 35 {
		t.Fatalf("unexpected inner local declaration self-ref: %+v", localDeclRefs[0])
	}
	if localDeclRefs[1].Start.Line != 9 || localDeclRefs[1].Start.Character != 43 {
		t.Fatalf("unexpected inner local declaration use ref: %+v", localDeclRefs[1])
	}

	outerRefs := LookupReferences(index, Position{Line: 9, Character: 57}, false)
	if len(outerRefs) != 2 {
		t.Fatalf("expected 2 outer local references without declaration, got %d", len(outerRefs))
	}
	if outerRefs[0].Start.Line != 9 || outerRefs[0].Start.Character != 24 {
		t.Fatalf("unexpected first outer local use ref: %+v", outerRefs[0])
	}
	if outerRefs[1].Start.Line != 9 || outerRefs[1].Start.Character != 57 {
		t.Fatalf("unexpected second outer local use ref: %+v", outerRefs[1])
	}
}

func TestLookupRenameEdits(t *testing.T) {
	path := filepath.Join("..", "..", "testdata", "symbols.fth")
	src, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read fixture: %v", err)
	}

	index := IndexDocument(string(src))

	wordEdits := LookupRenameEdits(index, Position{Line: 6, Character: 2})
	if len(wordEdits) != 2 {
		t.Fatalf("expected 2 rename edits for square, got %d", len(wordEdits))
	}
	if wordEdits[0].Start.Line != 1 || wordEdits[0].Start.Character != 2 {
		t.Fatalf("unexpected square declaration rename edit: %+v", wordEdits[0])
	}
	if wordEdits[1].Start.Line != 6 || wordEdits[1].Start.Character != 2 {
		t.Fatalf("unexpected square use rename edit: %+v", wordEdits[1])
	}

	localDeclEdits := LookupRenameEdits(index, Position{Line: 9, Character: 35})
	if len(localDeclEdits) != 2 {
		t.Fatalf("expected 2 rename edits for inner local declaration, got %d", len(localDeclEdits))
	}
	if localDeclEdits[0].Start.Line != 9 || localDeclEdits[0].Start.Character != 35 {
		t.Fatalf("unexpected inner local declaration rename edit: %+v", localDeclEdits[0])
	}
	if localDeclEdits[1].Start.Line != 9 || localDeclEdits[1].Start.Character != 43 {
		t.Fatalf("unexpected inner local use rename edit: %+v", localDeclEdits[1])
	}

	localUseEdits := LookupRenameEdits(index, Position{Line: 9, Character: 57})
	if len(localUseEdits) != 3 {
		t.Fatalf("expected 3 rename edits for outer local, got %d", len(localUseEdits))
	}
	if localUseEdits[0].Start.Line != 9 || localUseEdits[0].Start.Character != 16 {
		t.Fatalf("unexpected outer local declaration rename edit: %+v", localUseEdits[0])
	}
	if localUseEdits[1].Start.Line != 9 || localUseEdits[1].Start.Character != 24 {
		t.Fatalf("unexpected first outer local use rename edit: %+v", localUseEdits[1])
	}
	if localUseEdits[2].Start.Line != 9 || localUseEdits[2].Start.Character != 57 {
		t.Fatalf("unexpected second outer local use rename edit: %+v", localUseEdits[2])
	}
}
