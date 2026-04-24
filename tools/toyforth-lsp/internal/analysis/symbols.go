package analysis

import (
	"strings"

	tree_sitter "github.com/tree-sitter/go-tree-sitter"

	"toyforth-lsp/internal/parser"
)

type Position struct {
	Line      int `json:"line"`
	Character int `json:"character"`
}

type Range struct {
	Start Position `json:"start"`
	End   Position `json:"end"`
}

type Symbol struct {
	Name           string
	Kind           int
	Range          Range
	SelectionRange Range
	Detail         string
	Doc            string
	StackEffect    string
}

type tokenKind int

const (
	tokenKindWord tokenKind = iota
	tokenKindVariable
)

type Token struct {
	Text  string
	Range Range
	Kind  tokenKind
}

type LocalBinding struct {
	Name       string
	Range      Range
	ScopeRange Range
	ScopeDepth int
}

type DocumentIndex struct {
	Symbols     []Symbol
	Definitions map[string]Symbol
	WordTokens  []Token
	Locals      []LocalBinding
}

const (
	symbolKindFunction = 12
)

func IndexDocument(src string) DocumentIndex {
	index := DocumentIndex{
		Definitions: make(map[string]Symbol),
	}

	tree, err := parser.Parse([]byte(src))
	if err != nil || tree == nil {
		return index
	}
	defer tree.Close()

	root := tree.RootNode()
	if root == nil {
		return index
	}

	collectTopLevelDefinitions(root, []byte(src), &index)
	collectTokensAndLocals(root, []byte(src), &index, []Range{nodeRange(root)})

	return index
}

func DocumentSymbols(src string) []Symbol {
	return IndexDocument(src).Symbols
}

func LookupWordAt(index DocumentIndex, pos Position) (string, bool) {
	word, _, ok := lookupTokenAt(index, pos)
	return word, ok
}

func LookupDefinition(index DocumentIndex, pos Position) (Symbol, bool) {
	word, tok, ok := lookupTokenAt(index, pos)
	if !ok {
		if local, ok := lookupLocalBindingDeclarationAt(index, pos); ok {
			return Symbol{
				Name:           local.Name,
				Kind:           symbolKindFunction,
				Detail:         "local binding",
				Range:          local.Range,
				SelectionRange: local.Range,
			}, true
		}
		return Symbol{}, false
	}

	if tok.Kind == tokenKindVariable {
		local, ok := lookupLocalBinding(index, word, pos)
		if !ok {
			return Symbol{}, false
		}
		return Symbol{
			Name:           local.Name,
			Kind:           symbolKindFunction,
			Detail:         "local binding",
			Range:          local.Range,
			SelectionRange: local.Range,
		}, true
	}

	sym, ok := index.Definitions[word]
	return sym, ok
}

func LookupReferences(index DocumentIndex, pos Position, includeDeclaration bool) []Range {
	word, tok, ok := lookupTokenAt(index, pos)
	if !ok {
		if local, ok := lookupLocalBindingDeclarationAt(index, pos); ok {
			return localReferences(index, local, includeDeclaration)
		}
		return nil
	}

	if tok.Kind == tokenKindVariable {
		local, ok := lookupLocalBinding(index, word, pos)
		if !ok {
			return nil
		}
		return localReferences(index, local, includeDeclaration)
	}

	sym, ok := index.Definitions[word]
	if !ok {
		return nil
	}

	refs := make([]Range, 0)
	if includeDeclaration {
		refs = append(refs, sym.SelectionRange)
	}
	for _, candidate := range index.WordTokens {
		if candidate.Kind != tokenKindWord || candidate.Text != sym.Name {
			continue
		}
		if sameRange(candidate.Range, sym.SelectionRange) {
			continue
		}
		refs = append(refs, candidate.Range)
	}
	return refs
}

func LookupRenameEdits(index DocumentIndex, pos Position) []Range {
	word, tok, ok := lookupTokenAt(index, pos)
	if !ok {
		if local, ok := lookupLocalBindingDeclarationAt(index, pos); ok {
			return localReferences(index, local, true)
		}
		return nil
	}

	if tok.Kind == tokenKindVariable {
		local, ok := lookupLocalBinding(index, word, pos)
		if !ok {
			return nil
		}
		return localReferences(index, local, true)
	}

	sym, ok := index.Definitions[word]
	if !ok {
		return nil
	}

	edits := make([]Range, 0, 1)
	edits = append(edits, sym.SelectionRange)
	for _, candidate := range index.WordTokens {
		if candidate.Kind != tokenKindWord || candidate.Text != sym.Name {
			continue
		}
		if sameRange(candidate.Range, sym.SelectionRange) {
			continue
		}
		edits = append(edits, candidate.Range)
	}
	return edits
}

func localReferences(index DocumentIndex, local LocalBinding, includeDeclaration bool) []Range {
	refs := make([]Range, 0)
	if includeDeclaration {
		refs = append(refs, local.Range)
	}
	for _, candidate := range index.WordTokens {
		if candidate.Kind != tokenKindVariable || candidate.Text != local.Name {
			continue
		}
		resolved, ok := lookupLocalBinding(index, candidate.Text, candidate.Range.Start)
		if !ok || !sameRange(resolved.Range, local.Range) {
			continue
		}
		refs = append(refs, candidate.Range)
	}
	return refs
}

func lookupTokenAt(index DocumentIndex, pos Position) (string, Token, bool) {
	for _, tok := range index.WordTokens {
		if containsPosition(tok.Range, pos) {
			return tok.Text, tok, true
		}
	}
	return "", Token{}, false
}

func collectTokensAndLocals(node *tree_sitter.Node, src []byte, index *DocumentIndex, scopes []Range) {
	if node == nil {
		return
	}

	currentScopes := scopes

	switch node.Kind() {
	case "source_file":
		currentScopes = []Range{nodeRange(node)}
	case "colon_definition", "block":
		currentScopes = append(append([]Range(nil), scopes...), nodeRange(node))
	case "var_fetch":
		if child := node.NamedChild(0); child != nil && child.Kind() == "variable_name" {
			index.WordTokens = append(index.WordTokens, Token{
				Text:  child.Utf8Text(src),
				Range: nodeRange(node),
				Kind:  tokenKindVariable,
			})
		}
		return
	case "var_list":
		collectLocalBindings(node, src, index, currentScopes)
		return
	case "word", "definition_name", "symbol_name", "builtin_word", "control_word", "operator":
		index.WordTokens = append(index.WordTokens, Token{
			Text:  node.Utf8Text(src),
			Range: nodeRange(node),
			Kind:  tokenKindWord,
		})
	}

	for i := uint(0); i < node.NamedChildCount(); i++ {
		collectTokensAndLocals(node.NamedChild(i), src, index, currentScopes)
	}
}

func collectLocalBindings(node *tree_sitter.Node, src []byte, index *DocumentIndex, scopes []Range) {
	if len(scopes) == 0 {
		return
	}

	scope := scopes[len(scopes)-1]
	scopeDepth := len(scopes) - 1

	for i := uint(0); i < node.NamedChildCount(); i++ {
		child := node.NamedChild(i)
		if child == nil || child.Kind() != "word" {
			continue
		}

		index.Locals = append(index.Locals, LocalBinding{
			Name:       child.Utf8Text(src),
			Range:      nodeRange(child),
			ScopeRange: scope,
			ScopeDepth: scopeDepth,
		})
	}
}

func collectTopLevelDefinitions(root *tree_sitter.Node, src []byte, index *DocumentIndex) {
	for i := uint(0); i < root.NamedChildCount(); i++ {
		child := root.NamedChild(i)
		if child == nil {
			continue
		}

		switch child.Kind() {
		case "colon_definition":
			if sym, ok := indexColonDefinition(child, leadingLineDoc(root, i, src), src); ok {
				index.Symbols = append(index.Symbols, sym)
				index.Definitions[sym.Name] = sym
			}
		case "quoted_symbol":
			if sym, ok := indexDefDefinition(root, i, leadingLineDoc(root, i, src), src); ok {
				index.Symbols = append(index.Symbols, sym)
				index.Definitions[sym.Name] = sym
			}
		}
	}
}

func indexColonDefinition(node *tree_sitter.Node, doc string, src []byte) (Symbol, bool) {
	var stackEffect string
	var nameNode *tree_sitter.Node

	for i := uint(0); i < node.NamedChildCount(); i++ {
		child := node.NamedChild(i)
		if child != nil && child.Kind() == "block_comment" && stackEffect == "" {
			stackEffect = trimParenComment(child.Utf8Text(src))
		}
		if child != nil && child.Kind() == "definition_name" {
			nameNode = child
		}
	}

	if nameNode == nil {
		return Symbol{}, false
	}

	return Symbol{
		Name:           nameNode.Utf8Text(src),
		Kind:           symbolKindFunction,
		Detail:         "colon definition",
		Range:          nodeRange(node),
		SelectionRange: nodeRange(nameNode),
		Doc:            doc,
		StackEffect:    stackEffect,
	}, true
}

func indexDefDefinition(root *tree_sitter.Node, i uint, doc string, src []byte) (Symbol, bool) {
	quoted := root.NamedChild(i)
	if quoted == nil || quoted.Kind() != "quoted_symbol" {
		return Symbol{}, false
	}

	nameNode := quoted.NamedChild(0)
	if nameNode == nil || nameNode.Kind() != "symbol_name" {
		return Symbol{}, false
	}

	block := root.NamedChild(i + 1)
	defWord := root.NamedChild(i + 2)
	if block == nil || defWord == nil {
		return Symbol{}, false
	}
	if block.Kind() != "block" || defWord.Kind() != "builtin_word" || defWord.Utf8Text(src) != "def" {
		return Symbol{}, false
	}

	return Symbol{
		Name:   nameNode.Utf8Text(src),
		Kind:   symbolKindFunction,
		Detail: "quoted symbol def",
		Range: Range{
			Start: nodeRange(quoted).Start,
			End:   nodeRange(defWord).End,
		},
		SelectionRange: nodeRange(nameNode),
		Doc:            doc,
	}, true
}

func leadingLineDoc(root *tree_sitter.Node, defIndex uint, src []byte) string {
	if defIndex == 0 {
		return ""
	}

	lines := make([]string, 0)
	defStart := root.NamedChild(defIndex).StartPosition().Row
	expectedEndRow := int(defStart) - 1

	for j := int(defIndex) - 1; j >= 0; j-- {
		node := root.NamedChild(uint(j))
		if node == nil {
			break
		}
		if node.Kind() != "line_comment" {
			break
		}

		nodeEnd := int(node.EndPosition().Row)
		if nodeEnd != expectedEndRow {
			break
		}

		lines = append([]string{trimLineComment(node.Utf8Text(src))}, lines...)
		expectedEndRow = int(node.StartPosition().Row) - 1
	}

	return strings.TrimSpace(strings.Join(lines, "\n"))
}

func lookupLocalBinding(index DocumentIndex, name string, pos Position) (LocalBinding, bool) {
	var (
		best   LocalBinding
		bestOK bool
	)

	for _, local := range index.Locals {
		if local.Name != name {
			continue
		}
		if !containsPosition(local.ScopeRange, pos) {
			continue
		}
		if comparePosition(local.Range.Start, pos) > 0 {
			continue
		}
		if !bestOK || local.ScopeDepth > best.ScopeDepth || (local.ScopeDepth == best.ScopeDepth && comparePosition(local.Range.Start, best.Range.Start) > 0) {
			best = local
			bestOK = true
		}
	}

	return best, bestOK
}

func lookupLocalBindingDeclarationAt(index DocumentIndex, pos Position) (LocalBinding, bool) {
	var (
		best   LocalBinding
		bestOK bool
	)

	for _, local := range index.Locals {
		if !containsPosition(local.Range, pos) {
			continue
		}
		if !bestOK || local.ScopeDepth > best.ScopeDepth || (local.ScopeDepth == best.ScopeDepth && comparePosition(local.Range.Start, best.Range.Start) > 0) {
			best = local
			bestOK = true
		}
	}

	return best, bestOK
}

func nodeRange(node *tree_sitter.Node) Range {
	start := node.StartPosition()
	end := node.EndPosition()
	return Range{
		Start: Position{
			Line:      int(start.Row),
			Character: int(start.Column),
		},
		End: Position{
			Line:      int(end.Row),
			Character: int(end.Column),
		},
	}
}

func containsPosition(rng Range, pos Position) bool {
	if pos.Line < rng.Start.Line || pos.Line > rng.End.Line {
		return false
	}
	if pos.Line == rng.Start.Line && pos.Character < rng.Start.Character {
		return false
	}
	if pos.Line == rng.End.Line && pos.Character >= rng.End.Character {
		return false
	}
	return true
}

func comparePosition(a Position, b Position) int {
	if a.Line < b.Line {
		return -1
	}
	if a.Line > b.Line {
		return 1
	}
	if a.Character < b.Character {
		return -1
	}
	if a.Character > b.Character {
		return 1
	}
	return 0
}

func sameRange(a Range, b Range) bool {
	return a.Start == b.Start && a.End == b.End
}

func trimParenComment(text string) string {
	if len(text) >= 2 && text[0] == '(' && text[len(text)-1] == ')' {
		text = text[1 : len(text)-1]
	}
	return strings.TrimSpace(text)
}

func trimLineComment(text string) string {
	if len(text) > 0 && text[0] == '\\' {
		text = text[1:]
	}
	return strings.TrimSpace(text)
}
