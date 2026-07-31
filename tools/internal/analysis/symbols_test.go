package analysis

import (
	"os"
	"path/filepath"
	"testing"
)

func TestDocumentSymbols(t *testing.T) {
	path := filepath.Join("..", "..", "testdata", "symbols.toy")
	src, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read fixture: %v", err)
	}

	symbols := DocumentSymbols(string(src))
	if len(symbols) != 3 {
		t.Fatalf("expected 3 symbols, got %d", len(symbols))
	}

	if symbols[0].Name != "sqr" || symbols[0].Detail != "symbol definition" {
		t.Fatalf("unexpected first symbol: %+v", symbols[0])
	}
	if symbols[0].Doc != "top-level definitions" {
		t.Fatalf("unexpected sqr doc: %+v", symbols[0])
	}
	if symbols[0].StackEffect != "" {
		t.Fatalf("unexpected sqr stack effect: %+v", symbols[0])
	}
	if symbols[0].SelectionRange.Start.Character != 1 {
		t.Fatalf("unexpected sqr selection range: %+v", symbols[0].SelectionRange)
	}

	if symbols[1].Name != "pow3" || symbols[1].Detail != "symbol definition" {
		t.Fatalf("unexpected second symbol: %+v", symbols[1])
	}
	if symbols[1].Doc != "definition comment" {
		t.Fatalf("unexpected pow3 doc: %+v", symbols[1])
	}
	if symbols[1].SelectionRange.Start.Character != 1 {
		t.Fatalf("unexpected pow3 selection range: %+v", symbols[1].SelectionRange)
	}

	if symbols[2].Name != "shadow-demo" || symbols[2].Detail != "symbol definition" {
		t.Fatalf("unexpected third symbol: %+v", symbols[2])
	}
}

func TestLookupDefinition(t *testing.T) {
	path := filepath.Join("..", "..", "testdata", "symbols.toy")
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
			name: "call to sqr resolves",
			pos: Position{
				Line:      6,
				Character: 2,
			},
			want: "sqr",
		},
		{
			name: "call to pow3 resolves",
			pos: Position{
				Line:      7,
				Character: 2,
			},
			want: "pow3",
		},
		{
			name: "local fetch resolves to local binding",
			pos: Position{
				Line:      1,
				Character: 14,
			},
			want: "n",
		},
		{
			name: "nested local fetch resolves to inner binding",
			pos: Position{
				Line:      9,
				Character: 45,
			},
			want: "outer",
		},
		{
			name: "post-block local fetch resolves to outer binding",
			pos: Position{
				Line:      9,
				Character: 59,
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
			if sym.SelectionRange.Start.Character >= sym.Range.Start.Character && sym.Name == "sqr" && sym.SelectionRange.Start.Character != 1 {
				t.Fatalf("unexpected sqr target range: %+v", sym)
			}
			if tc.name == "local fetch resolves to local binding" && sym.SelectionRange.Start.Character != 9 {
				t.Fatalf("unexpected local binding target: %+v", sym)
			}
			if tc.name == "nested local fetch resolves to inner binding" && sym.SelectionRange.Start.Character != 36 {
				t.Fatalf("unexpected inner local target: %+v", sym)
			}
			if tc.name == "post-block local fetch resolves to outer binding" && sym.SelectionRange.Start.Character != 17 {
				t.Fatalf("unexpected outer local target: %+v", sym)
			}
		})
	}
}

func TestLookupHover(t *testing.T) {
	path := filepath.Join("..", "..", "testdata", "symbols.toy")
	src, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read fixture: %v", err)
	}

	index := IndexDocument(string(src))

	userHover, ok := LookupHover(index, Position{Line: 7, Character: 2})
	if !ok {
		t.Fatalf("expected hover for user word")
	}
	if userHover.Contents != "```toy\npow3\n```\ndefinition comment" {
		t.Fatalf("unexpected user hover: %q", userHover.Contents)
	}

	stackEffectHover, ok := LookupHover(index, Position{Line: 6, Character: 2})
	if !ok {
		t.Fatalf("expected hover for user definition call")
	}
	if stackEffectHover.Contents != "```toy\nsqr\n```\ntop-level definitions" {
		t.Fatalf("unexpected stack effect hover: %q", stackEffectHover.Contents)
	}

	localHover, ok := LookupHover(index, Position{Line: 9, Character: 45})
	if !ok {
		t.Fatalf("expected hover for local binding")
	}
	if localHover.Contents != "```toy\n$outer\n```\nLocal binding from `| outer |`." {
		t.Fatalf("unexpected local hover: %q", localHover.Contents)
	}

	builtinHover, ok := LookupHover(index, Position{Line: 15, Character: 8})
	if !ok {
		t.Fatalf("expected hover for builtin word")
	}
	if builtinHover.Range.Start.Character != 7 {
		t.Fatalf("unexpected builtin hover range: %+v", builtinHover.Range)
	}
	if builtinHover.Contents != "```toy\nprint\n```\nstack: `x --`\n\nPrint one value literally, followed by a newline." {
		t.Fatalf("unexpected builtin hover contents: %q", builtinHover.Contents)
	}
}

func TestLookupReferences(t *testing.T) {
	path := filepath.Join("..", "..", "testdata", "symbols.toy")
	src, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read fixture: %v", err)
	}

	index := IndexDocument(string(src))

	wordRefs := LookupReferences(index, Position{Line: 6, Character: 2}, true)
	if len(wordRefs) != 2 {
		t.Fatalf("expected 2 references for sqr including declaration, got %d", len(wordRefs))
	}
	if wordRefs[0].Start.Line != 1 || wordRefs[0].Start.Character != 1 {
		t.Fatalf("unexpected sqr declaration ref: %+v", wordRefs[0])
	}
	if wordRefs[1].Start.Line != 6 || wordRefs[1].Start.Character != 2 {
		t.Fatalf("unexpected sqr call ref: %+v", wordRefs[1])
	}

	localRefs := LookupReferences(index, Position{Line: 9, Character: 45}, true)
	if len(localRefs) != 2 {
		t.Fatalf("expected 2 references for inner outer including declaration, got %d", len(localRefs))
	}
	if localRefs[0].Start.Line != 9 || localRefs[0].Start.Character != 36 {
		t.Fatalf("unexpected inner local declaration ref: %+v", localRefs[0])
	}
	if localRefs[1].Start.Line != 9 || localRefs[1].Start.Character != 44 {
		t.Fatalf("unexpected inner local use ref: %+v", localRefs[1])
	}

	localDeclRefs := LookupReferences(index, Position{Line: 9, Character: 36}, true)
	if len(localDeclRefs) != 2 {
		t.Fatalf("expected 2 references when invoked on inner local declaration, got %d", len(localDeclRefs))
	}
	if localDeclRefs[0].Start.Line != 9 || localDeclRefs[0].Start.Character != 36 {
		t.Fatalf("unexpected inner local declaration self-ref: %+v", localDeclRefs[0])
	}
	if localDeclRefs[1].Start.Line != 9 || localDeclRefs[1].Start.Character != 44 {
		t.Fatalf("unexpected inner local declaration use ref: %+v", localDeclRefs[1])
	}

	outerRefs := LookupReferences(index, Position{Line: 9, Character: 59}, false)
	if len(outerRefs) != 2 {
		t.Fatalf("expected 2 outer local references without declaration, got %d", len(outerRefs))
	}
	if outerRefs[0].Start.Line != 9 || outerRefs[0].Start.Character != 25 {
		t.Fatalf("unexpected first outer local use ref: %+v", outerRefs[0])
	}
	if outerRefs[1].Start.Line != 9 || outerRefs[1].Start.Character != 58 {
		t.Fatalf("unexpected second outer local use ref: %+v", outerRefs[1])
	}
}

func TestLookupRenameEdits(t *testing.T) {
	path := filepath.Join("..", "..", "testdata", "symbols.toy")
	src, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read fixture: %v", err)
	}

	index := IndexDocument(string(src))

	wordEdits := LookupRenameEdits(index, Position{Line: 6, Character: 2})
	if len(wordEdits) != 2 {
		t.Fatalf("expected 2 rename edits for sqr, got %d", len(wordEdits))
	}
	if wordEdits[0].Start.Line != 1 || wordEdits[0].Start.Character != 1 {
		t.Fatalf("unexpected sqr declaration rename edit: %+v", wordEdits[0])
	}
	if wordEdits[1].Start.Line != 6 || wordEdits[1].Start.Character != 2 {
		t.Fatalf("unexpected sqr use rename edit: %+v", wordEdits[1])
	}

	localDeclEdits := LookupRenameEdits(index, Position{Line: 9, Character: 36})
	if len(localDeclEdits) != 2 {
		t.Fatalf("expected 2 rename edits for inner local declaration, got %d", len(localDeclEdits))
	}
	if localDeclEdits[0].Start.Line != 9 || localDeclEdits[0].Start.Character != 36 {
		t.Fatalf("unexpected inner local declaration rename edit: %+v", localDeclEdits[0])
	}
	if localDeclEdits[1].Start.Line != 9 || localDeclEdits[1].Start.Character != 45 {
		t.Fatalf("unexpected inner local use rename edit: %+v", localDeclEdits[1])
	}

	localUseEdits := LookupRenameEdits(index, Position{Line: 9, Character: 59})
	if len(localUseEdits) != 3 {
		t.Fatalf("expected 3 rename edits for outer local, got %d", len(localUseEdits))
	}
	if localUseEdits[0].Start.Line != 9 || localUseEdits[0].Start.Character != 17 {
		t.Fatalf("unexpected outer local declaration rename edit: %+v", localUseEdits[0])
	}
	if localUseEdits[1].Start.Line != 9 || localUseEdits[1].Start.Character != 26 {
		t.Fatalf("unexpected first outer local use rename edit: %+v", localUseEdits[1])
	}
	if localUseEdits[2].Start.Line != 9 || localUseEdits[2].Start.Character != 59 {
		t.Fatalf("unexpected second outer local use rename edit: %+v", localUseEdits[2])
	}
}

func TestIndexPackageDirectives(t *testing.T) {
	source := `'main package
"../math" import
"../util/math" 'm import-as
'double [ 2 * ] def
'double private
[ "../nested" 'n import-as ]`

	index := IndexDocument(source)
	if index.PackageName != "main" {
		t.Fatalf("package = %q, want main", index.PackageName)
	}
	if len(index.Imports) != 2 {
		t.Fatalf("imports = %+v, want 2", index.Imports)
	}
	if index.Imports[0].Path != "../math" || index.Imports[0].Alias != "" {
		t.Fatalf("first import = %+v", index.Imports[0])
	}
	if index.Imports[1].Path != "../util/math" || index.Imports[1].Alias != "m" {
		t.Fatalf("aliased import = %+v", index.Imports[1])
	}
	if index.IsPublic("double") || !index.IsPublic("missing") {
		t.Fatalf("privates = %+v", index.Privates)
	}

	packageRef, ok := LookupSourceReferenceAt(index, Position{Line: 2, Character: 3})
	if !ok || packageRef.Kind != SourceReferencePackage || packageRef.Target != "../util/math" {
		t.Fatalf("package source reference = %+v, %v", packageRef, ok)
	}
	if _, ok := LookupSourceReferenceAt(index, Position{Line: 3, Character: 3}); ok {
		t.Fatal("definition name unexpectedly treated as a source reference")
	}
}

func TestIndexUsesUTF16Columns(t *testing.T) {
	source := "\"😀\" 'target [ 1 ] def\n\"😀\" target"
	index := IndexDocument(source)

	sym := index.Definitions["target"]
	if sym.SelectionRange.Start.Character != 6 {
		t.Fatalf("definition starts at UTF-16 column %d, want 6", sym.SelectionRange.Start.Character)
	}
	resolved, ok := LookupDefinition(index, Position{Line: 1, Character: 6})
	if !ok || resolved.Name != "target" {
		t.Fatalf("UTF-16 call did not resolve: %+v, %v", resolved, ok)
	}
}

func TestQuotedSymbolIsOnlyAReferenceAtDefinitionAndPrivacySites(t *testing.T) {
	index := IndexDocument(`'foo [ 1 ] def
'foo private
'foo print`)

	if _, ok := LookupDefinition(index, Position{Line: 2, Character: 2}); ok {
		t.Fatal("ordinary quoted symbol unexpectedly resolved as a word reference")
	}
	if _, ok := LookupHover(index, Position{Line: 2, Character: 2}); ok {
		t.Fatal("ordinary quoted symbol unexpectedly received function hover")
	}
	refs := LookupReferences(index, Position{Line: 0, Character: 2}, true)
	if len(refs) != 2 {
		t.Fatalf("references = %+v, want definition plus privacy marker", refs)
	}
}
