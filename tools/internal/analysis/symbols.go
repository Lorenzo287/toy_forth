package analysis

import (
	"strconv"
	"strings"
	"unicode/utf16"
	"unicode/utf8"

	tree_sitter "github.com/tree-sitter/go-tree-sitter"

	"toy-tools/internal/parser"
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
	tokenKindSymbol
)

type Token struct {
	Text  string
	Range Range
	Kind  tokenKind
}

func (token Token) IsWord() bool {
	return token.Kind == tokenKindWord
}

type LocalBinding struct {
	Name       string
	Range      Range
	ScopeRange Range
	ScopeDepth int
}

type PackageImport struct {
	Path        string
	Alias       string
	Range       Range
	SourceRange Range
}

type SourceReferenceKind int

const (
	SourceReferencePackage SourceReferenceKind = iota
)

type SourceReference struct {
	Kind   SourceReferenceKind
	Target string
	Range  Range
}

type Privacy struct {
	Name  string
	Range Range
}

type DocumentIndex struct {
	Symbols      []Symbol
	Definitions  map[string]Symbol
	WordTokens   []Token
	Locals       []LocalBinding
	PackageName  string
	PackageRange Range
	Imports      []PackageImport
	Privates     []Privacy
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

	source := []byte(src)
	positions := newPositionMapper(source)
	collectTopLevelDefinitions(root, source, &index, positions)
	collectPackageDirectives(root, source, &index, positions)
	collectTokensAndLocals(root, source, &index, []Range{positions.nodeRange(root)}, positions)

	return index
}

func DocumentSymbols(src string) []Symbol {
	return IndexDocument(src).Symbols
}

func LookupWordAt(index DocumentIndex, pos Position) (string, bool) {
	word, _, ok := lookupTokenAt(index, pos)
	return word, ok
}

func LookupTokenAt(index DocumentIndex, pos Position) (Token, bool) {
	_, tok, ok := lookupTokenAt(index, pos)
	return tok, ok
}

func (index DocumentIndex) IsPublic(name string) bool {
	for _, private := range index.Privates {
		if private.Name == name {
			return false
		}
	}
	return true
}

func LookupSourceReferenceAt(index DocumentIndex, pos Position) (SourceReference, bool) {
	for _, imported := range index.Imports {
		if containsPosition(imported.SourceRange, pos) {
			return SourceReference{
				Kind:   SourceReferencePackage,
				Target: imported.Path,
				Range:  imported.SourceRange,
			}, true
		}
	}
	return SourceReference{}, false
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
	if tok.Kind == tokenKindSymbol {
		sym, ok := index.Definitions[word]
		if !ok || (!sameRange(tok.Range, sym.SelectionRange) && !isPrivacyRange(index, word, tok.Range)) {
			return Symbol{}, false
		}
		return sym, true
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
	if tok.Kind == tokenKindSymbol && !isPrivacyRange(index, word, tok.Range) && !isDefinitionRange(index, word, tok.Range) {
		return nil
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
	for _, private := range index.Privates {
		if private.Name == sym.Name {
			refs = append(refs, private.Range)
		}
	}
	return refs
}

func LookupRenameEdits(index DocumentIndex, pos Position) []Range {
	word, tok, ok := lookupTokenAt(index, pos)
	if !ok {
		if local, ok := lookupLocalBindingDeclarationAt(index, pos); ok {
			return localRenameEdits(index, local)
		}
		return nil
	}

	if tok.Kind == tokenKindVariable {
		local, ok := lookupLocalBinding(index, word, pos)
		if !ok {
			return nil
		}
		return localRenameEdits(index, local)
	}
	if tok.Kind == tokenKindSymbol && !isPrivacyRange(index, word, tok.Range) && !isDefinitionRange(index, word, tok.Range) {
		return nil
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
	for _, private := range index.Privates {
		if private.Name == sym.Name {
			edits = append(edits, private.Range)
		}
	}
	return edits
}

func localRenameEdits(index DocumentIndex, local LocalBinding) []Range {
	edits := []Range{local.Range}
	for _, candidate := range index.WordTokens {
		if candidate.Kind != tokenKindVariable || candidate.Text != local.Name {
			continue
		}
		resolved, ok := lookupLocalBinding(index, candidate.Text, candidate.Range.Start)
		if !ok || !sameRange(resolved.Range, local.Range) {
			continue
		}
		rng := candidate.Range
		// Variable token ranges include the `$` so hover and references cover
		// the complete fetch. Rename only the identifier that follows it.
		rng.Start.Character++
		edits = append(edits, rng)
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

func collectTokensAndLocals(node *tree_sitter.Node, src []byte, index *DocumentIndex, scopes []Range, positions *positionMapper) {
	if node == nil {
		return
	}

	currentScopes := scopes

	switch node.Kind() {
	case "source_file":
		currentScopes = []Range{positions.nodeRange(node)}
	case "block":
		currentScopes = append(append([]Range(nil), scopes...), positions.nodeRange(node))
	case "var_fetch":
		if child := node.NamedChild(0); child != nil && child.Kind() == "variable_name" {
			index.WordTokens = append(index.WordTokens, Token{
				Text:  child.Utf8Text(src),
				Range: positions.nodeRange(node),
				Kind:  tokenKindVariable,
			})
		}
		return
	case "var_list":
		collectLocalBindings(node, src, index, currentScopes, positions)
		return
	case "symbol_name":
		index.WordTokens = append(index.WordTokens, Token{
			Text:  node.Utf8Text(src),
			Range: positions.nodeRange(node),
			Kind:  tokenKindSymbol,
		})
	case "word", "builtin_word", "control_word", "operator":
		index.WordTokens = append(index.WordTokens, Token{
			Text:  node.Utf8Text(src),
			Range: positions.nodeRange(node),
			Kind:  tokenKindWord,
		})
	}

	for i := uint(0); i < node.NamedChildCount(); i++ {
		collectTokensAndLocals(node.NamedChild(i), src, index, currentScopes, positions)
	}
}

func collectLocalBindings(node *tree_sitter.Node, src []byte, index *DocumentIndex, scopes []Range, positions *positionMapper) {
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
			Range:      positions.nodeRange(child),
			ScopeRange: scope,
			ScopeDepth: scopeDepth,
		})
	}
}

func collectTopLevelDefinitions(root *tree_sitter.Node, src []byte, index *DocumentIndex, positions *positionMapper) {
	for i := uint(0); i < root.NamedChildCount(); i++ {
		child := root.NamedChild(i)
		if child == nil {
			continue
		}

		switch child.Kind() {
		case "quoted_symbol":
			if sym, ok := indexDefDefinition(root, i, leadingLineDoc(root, i, src), src, positions); ok {
				index.Symbols = append(index.Symbols, sym)
				index.Definitions[sym.Name] = sym
			}
		}
	}
}

func indexDefDefinition(root *tree_sitter.Node, i uint, doc string, src []byte, positions *positionMapper) (Symbol, bool) {
	quoted := root.NamedChild(i)
	if quoted == nil || quoted.Kind() != "quoted_symbol" {
		return Symbol{}, false
	}

	nameNode := quoted.NamedChild(0)
	if nameNode == nil || nameNode.Kind() != "symbol_name" {
		return Symbol{}, false
	}

	blockIndex, block := nextExpressionChild(root, i+1)
	_, defWord := nextExpressionChild(root, blockIndex+1)
	if block == nil || defWord == nil {
		return Symbol{}, false
	}
	if block.Kind() != "block" || defWord.Kind() != "builtin_word" || defWord.Utf8Text(src) != "def" {
		return Symbol{}, false
	}

	return Symbol{
		Name:   nameNode.Utf8Text(src),
		Kind:   symbolKindFunction,
		Detail: "symbol definition",
		Range: Range{
			Start: positions.nodeRange(quoted).Start,
			End:   positions.nodeRange(defWord).End,
		},
		SelectionRange: positions.nodeRange(nameNode),
		Doc:            doc,
	}, true
}

func collectPackageDirectives(root *tree_sitter.Node, src []byte, index *DocumentIndex, positions *positionMapper) {
	if root == nil {
		return
	}

	for i := uint(0); i < root.NamedChildCount(); i++ {
		child := root.NamedChild(i)
		if child == nil {
			continue
		}

		switch child.Kind() {
		case "string":
			value, ok := decodeStringLiteral(child.Utf8Text(src))
			if !ok {
				break
			}

			secondIndex, second := nextExpressionChild(root, i+1)
			if second == nil {
				break
			}
			if second.Kind() == "builtin_word" && second.Utf8Text(src) == "import" {
				index.Imports = append(index.Imports, PackageImport{
					Path:        value,
					SourceRange: positions.nodeRange(child),
					Range: Range{
						Start: positions.nodeRange(child).Start,
						End:   positions.nodeRange(second).End,
					},
				})
				break
			}

			if second.Kind() != "quoted_symbol" {
				break
			}
			aliasNode := second.NamedChild(0)
			if aliasNode == nil || aliasNode.Kind() != "symbol_name" {
				break
			}
			_, third := nextExpressionChild(root, secondIndex+1)
			if third == nil || third.Kind() != "builtin_word" || third.Utf8Text(src) != "import-as" {
				break
			}
			index.Imports = append(index.Imports, PackageImport{
				Path:        value,
				Alias:       aliasNode.Utf8Text(src),
				SourceRange: positions.nodeRange(child),
				Range: Range{
					Start: positions.nodeRange(child).Start,
					End:   positions.nodeRange(third).End,
				},
			})

		case "quoted_symbol":
			nameNode := child.NamedChild(0)
			if nameNode == nil || nameNode.Kind() != "symbol_name" {
				break
			}
			_, second := nextExpressionChild(root, i+1)
			if second == nil || second.Kind() != "builtin_word" {
				break
			}
			switch second.Utf8Text(src) {
			case "package":
				if index.PackageName == "" {
					index.PackageName = nameNode.Utf8Text(src)
					index.PackageRange = positions.nodeRange(nameNode)
				}
			case "private":
				index.Privates = append(index.Privates, Privacy{
					Name:  nameNode.Utf8Text(src),
					Range: positions.nodeRange(nameNode),
				})
			}
		}
	}
}

func nextExpressionChild(parent *tree_sitter.Node, start uint) (uint, *tree_sitter.Node) {
	for i := start; i < parent.NamedChildCount(); i++ {
		child := parent.NamedChild(i)
		if child == nil || child.Kind() == "line_comment" || child.Kind() == "block_comment" {
			continue
		}
		return i, child
	}
	return 0, nil
}

func decodeStringLiteral(text string) (string, bool) {
	value, err := strconv.Unquote(text)
	return value, err == nil
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

type positionMapper struct {
	source     []byte
	lineStarts []int
}

func newPositionMapper(source []byte) *positionMapper {
	mapper := &positionMapper{
		source:     source,
		lineStarts: []int{0},
	}
	for i, b := range source {
		if b == '\n' {
			mapper.lineStarts = append(mapper.lineStarts, i+1)
		}
	}
	return mapper
}

func (mapper *positionMapper) nodeRange(node *tree_sitter.Node) Range {
	start := node.StartPosition()
	end := node.EndPosition()
	return Range{
		Start: mapper.position(uint(start.Row), uint(start.Column)),
		End:   mapper.position(uint(end.Row), uint(end.Column)),
	}
}

func (mapper *positionMapper) position(row, byteColumn uint) Position {
	position := Position{Line: int(row)}
	if int(row) >= len(mapper.lineStarts) {
		return position
	}

	lineStart := mapper.lineStarts[row]
	columnEnd := lineStart + int(byteColumn)
	if columnEnd > len(mapper.source) {
		columnEnd = len(mapper.source)
	}
	for input := mapper.source[lineStart:columnEnd]; len(input) > 0; {
		r, size := utf8.DecodeRune(input)
		if size == 0 {
			break
		}
		width := utf16.RuneLen(r)
		if width < 0 {
			width = 1
		}
		position.Character += width
		input = input[size:]
	}
	return position
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

func isDefinitionRange(index DocumentIndex, name string, rng Range) bool {
	sym, ok := index.Definitions[name]
	return ok && sameRange(sym.SelectionRange, rng)
}

func isPrivacyRange(index DocumentIndex, name string, rng Range) bool {
	for _, private := range index.Privates {
		if private.Name == name && sameRange(private.Range, rng) {
			return true
		}
	}
	return false
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
