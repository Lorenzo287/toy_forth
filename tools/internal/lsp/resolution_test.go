package lsp

import (
	"bytes"
	"encoding/json"
	"io"
	"log"
	"os"
	"path/filepath"
	"testing"

	"toy-tools/internal/analysis"
)

func TestResolveDefinitionAcrossDirectoryPackagesAndAliases(t *testing.T) {
	root := t.TempDir()
	mathPath := writeToyFile(t, root, filepath.Join("math", "arithmetic.toy"), `'math package
\ math words
'double [ 2 * ] def
`)
	writeToyFile(t, root, filepath.Join("math", "internal.toy"), `'math package
'hidden [ 1 + ] def
'hidden private
`)
	appPath := writeToyFile(t, root, filepath.Join("app", "main.toy"), `'main package
"../math" import
21 math.double
math.hidden
"../math" 'm import-as
21 m.double
`)

	server := NewServer(log.New(io.Discard, "", 0))
	server.docs.configureWorkspace(initializeParams{RootURI: testFileURI(root)})
	appURI := testFileURI(appPath)

	canonical, ok := server.resolveDefinition(appURI, analysis.Position{Line: 2, Character: 9})
	if !ok || canonical.URI != testFileURI(mathPath) || canonical.Symbol.Name != "double" {
		t.Fatalf("canonical target = %+v, %v", canonical, ok)
	}
	if canonical.Symbol.SelectionRange.Start != (analysis.Position{Line: 2, Character: 1}) {
		t.Fatalf("canonical target range = %+v", canonical.Symbol.SelectionRange)
	}
	if _, ok := server.resolveDefinition(appURI, analysis.Position{Line: 3, Character: 7}); ok {
		t.Fatal("private package word unexpectedly resolved")
	}
	alias, ok := server.resolveDefinition(appURI, analysis.Position{Line: 5, Character: 5})
	if !ok || alias.URI != canonical.URI || alias.Symbol.Name != "double" {
		t.Fatalf("alias target = %+v, %v", alias, ok)
	}
}

func TestInitializeIndexesWorkspaceFiles(t *testing.T) {
	root := t.TempDir()
	path := writeToyFile(t, root, "indexed.toy", `'main package
'indexed [ 1 ] def`)
	params, err := json.Marshal(initializeParams{RootURI: testFileURI(root)})
	if err != nil {
		t.Fatal(err)
	}

	server := NewServer(log.New(io.Discard, "", 0))
	var output bytes.Buffer
	if err := server.handleInitialize(&output, request{ID: json.RawMessage("1"), Params: params}); err != nil {
		t.Fatal(err)
	}
	if _, ok := server.docs.get(testFileURI(path)); !ok {
		t.Fatal("workspace source was not indexed during initialize")
	}
}

func TestOpenPackageBufferOverridesDiskUntilClose(t *testing.T) {
	root := t.TempDir()
	mathPath := writeToyFile(t, root, filepath.Join("math", "math.toy"), `'math package
'disk-word [ 1 ] def`)
	appPath := writeToyFile(t, root, filepath.Join("app", "main.toy"), `'main package
"../math" import
math.buffer-word`)
	server := NewServer(log.New(io.Discard, "", 0))
	server.docs.configureWorkspace(initializeParams{RootURI: testFileURI(root)})

	mathURI := testFileURI(mathPath)
	server.docs.open(mathURI, `'math package
'buffer-word [ 2 ] def`, 1)
	if _, ok := server.resolveDefinition(testFileURI(appPath), analysis.Position{Line: 2, Character: 8}); !ok {
		t.Fatal("unsaved package definition did not override disk index")
	}
	server.docs.close(mathURI)
	if _, ok := server.resolveDefinition(testFileURI(appPath), analysis.Position{Line: 2, Character: 8}); ok {
		t.Fatal("closed package buffer did not fall back to disk index")
	}
}

func TestPackageImportsAreNotTransitive(t *testing.T) {
	root := t.TempDir()
	writeToyFile(t, root, filepath.Join("dependency", "dependency.toy"), `'dependency package
'value [ 5 ] def`)
	consumerPath := writeToyFile(t, root, filepath.Join("consumer", "consumer.toy"), `'consumer package
"../dependency" 'dep import-as
'combined [ dep.value 2 * ] def`)
	appPath := writeToyFile(t, root, filepath.Join("app", "main.toy"), `'main package
"../consumer" import
consumer.combined
dependency.value`)

	server := NewServer(log.New(io.Discard, "", 0))
	server.docs.configureWorkspace(initializeParams{RootURI: testFileURI(root)})
	appURI := testFileURI(appPath)
	target, ok := server.resolveDefinition(appURI, analysis.Position{Line: 2, Character: 12})
	if !ok || target.URI != testFileURI(consumerPath) || target.Symbol.Name != "combined" {
		t.Fatalf("consumer target = %+v, %v", target, ok)
	}
	if _, ok := server.resolveDefinition(appURI, analysis.Position{Line: 3, Character: 12}); ok {
		t.Fatal("transitive package unexpectedly became visible")
	}
}

func TestResolvePackageUsesExactRelativeDirectory(t *testing.T) {
	root := t.TempDir()
	writeToyFile(t, root, filepath.Join("math", "math.toy"), `'math package
'value [ 1 ] def`)
	nestedMath := writeToyFile(t, root, filepath.Join("nested", "math", "math.toy"), `'math package
'value [ 2 ] def`)
	appPath := writeToyFile(t, root, filepath.Join("nested", "app", "main.toy"), `'main package
"../math" import
math.value`)

	server := NewServer(log.New(io.Discard, "", 0))
	server.docs.configureWorkspace(initializeParams{RootURI: testFileURI(root)})
	target, ok := server.resolveDefinition(testFileURI(appPath), analysis.Position{Line: 2, Character: 6})
	if !ok || target.URI != testFileURI(nestedMath) {
		t.Fatalf("relative package target = %+v, %v", target, ok)
	}
}

func TestDefinitionResponseReturnsTargetDocumentURI(t *testing.T) {
	root := t.TempDir()
	mathPath := writeToyFile(t, root, filepath.Join("math", "math.toy"), `'math package
'double [ 2 * ] def`)
	appPath := writeToyFile(t, root, filepath.Join("app", "main.toy"), `'main package
"../math" import
math.double`)

	server := NewServer(log.New(io.Discard, "", 0))
	server.docs.configureWorkspace(initializeParams{RootURI: testFileURI(root)})
	params, err := json.Marshal(definitionParams{
		TextDocument: textDocumentIdentifier{URI: testFileURI(appPath)},
		Position:     lspPosition{Line: 2, Character: 7},
	})
	if err != nil {
		t.Fatal(err)
	}
	var output bytes.Buffer
	if err := server.handleDefinition(&output, request{ID: json.RawMessage("1"), Params: params}); err != nil {
		t.Fatal(err)
	}
	response := decodeTestResponse(t, output.Bytes())
	var target location
	if err := json.Unmarshal(response.Result, &target); err != nil {
		t.Fatal(err)
	}
	if target.URI != testFileURI(mathPath) || target.Range.Start != (lspPosition{Line: 1, Character: 1}) {
		t.Fatalf("definition response = %+v", target)
	}
}

func TestDefinitionResponseFollowsImportSourceStrings(t *testing.T) {
	root := t.TempDir()
	mathPath := writeToyFile(t, root, filepath.Join("math", "math.toy"), `'math package
'value [ 1 ] def`)
	appPath := writeToyFile(t, root, filepath.Join("app", "main.toy"), `'main package
"../math" import
"../math" 'm import-as`)

	server := NewServer(log.New(io.Discard, "", 0))
	server.docs.configureWorkspace(initializeParams{RootURI: testFileURI(root)})
	for _, position := range []lspPosition{{Line: 1, Character: 3}, {Line: 2, Character: 3}} {
		params, err := json.Marshal(definitionParams{
			TextDocument: textDocumentIdentifier{URI: testFileURI(appPath)},
			Position:     position,
		})
		if err != nil {
			t.Fatal(err)
		}
		var output bytes.Buffer
		if err := server.handleDefinition(&output, request{ID: json.RawMessage("1"), Params: params}); err != nil {
			t.Fatal(err)
		}
		response := decodeTestResponse(t, output.Bytes())
		var target location
		if err := json.Unmarshal(response.Result, &target); err != nil {
			t.Fatal(err)
		}
		if target.URI != testFileURI(mathPath) || target.Range != (lspRange{}) {
			t.Fatalf("source definition response = %+v", target)
		}
	}
}

func TestReferencesAndRenameFollowPackageIdentity(t *testing.T) {
	root := t.TempDir()
	mathPath := writeToyFile(t, root, filepath.Join("math", "math.toy"), `'math package
'double [ 2 * ] def
'use-double [ double ] def`)
	appPath := writeToyFile(t, root, filepath.Join("app", "main.toy"), `'main package
"../math" import
math.double
"../math" 'm import-as
m.double`)

	server := NewServer(log.New(io.Discard, "", 0))
	server.docs.configureWorkspace(initializeParams{RootURI: testFileURI(root)})
	appURI := testFileURI(appPath)
	target, ok := server.resolveDefinition(appURI, analysis.Position{Line: 2, Character: 7})
	if !ok {
		t.Fatal("package call did not resolve")
	}
	occurrences := server.wordOccurrences(target, true)
	if len(occurrences) != 4 {
		t.Fatalf("occurrences = %+v, want declaration, internal call, and two package calls", occurrences)
	}

	params, err := json.Marshal(renameParams{
		TextDocument: textDocumentIdentifier{URI: appURI},
		Position:     lspPosition{Line: 4, Character: 4},
		NewName:      "twice",
	})
	if err != nil {
		t.Fatal(err)
	}
	var output bytes.Buffer
	if err := server.handleRename(&output, request{ID: json.RawMessage("2"), Params: params}); err != nil {
		t.Fatal(err)
	}
	response := decodeTestResponse(t, output.Bytes())
	var edit workspaceEdit
	if err := json.Unmarshal(response.Result, &edit); err != nil {
		t.Fatal(err)
	}
	mathEdits := edit.Changes[testFileURI(mathPath)]
	appEdits := edit.Changes[appURI]
	if len(mathEdits) != 2 || len(appEdits) != 2 {
		t.Fatalf("rename changes = %+v", edit.Changes)
	}
	for _, change := range append(append([]textEdit(nil), mathEdits...), appEdits...) {
		if change.NewText != "twice" {
			t.Fatalf("rename edit = %+v", change)
		}
	}
}

func TestRenameCoversEveryFileInDirectoryPackagesAndImporters(t *testing.T) {
	root := t.TempDir()
	mathDefinition := writeToyFile(t, root, filepath.Join("math", "arithmetic.toy"), "'math package\n'double [ 2 * ] def\n")
	mathUse := writeToyFile(t, root, filepath.Join("math", "derived.toy"), "'math package\n'quad [ double 2 * ] def\n")
	appMain := writeToyFile(t, root, filepath.Join("app", "main.toy"), "'main package\n\"../math\" import\n'helper [ math.double ] def\n")
	appUse := writeToyFile(t, root, filepath.Join("app", "other.toy"), "'main package\nmath.double\n")

	server := NewServer(log.New(io.Discard, "", 0))
	server.docs.configureWorkspace(initializeParams{RootURI: testFileURI(root)})

	target, ok := server.resolveDefinition(testFileURI(appUse), analysis.Position{Line: 1, Character: 6})
	if !ok || target.URI != testFileURI(mathDefinition) {
		t.Fatalf("package target = %+v, %v", target, ok)
	}

	occurrences := server.wordOccurrences(target, true)
	byURI := make(map[string]int)
	for _, occurrence := range occurrences {
		byURI[occurrence.URI]++
	}
	want := map[string]int{
		testFileURI(mathDefinition): 1,
		testFileURI(mathUse):        1,
		testFileURI(appMain):        1,
		testFileURI(appUse):         1,
	}
	if len(occurrences) != len(want) {
		t.Fatalf("occurrences = %+v, want one edit in each package file and importer", occurrences)
	}
	for uri, count := range want {
		if byURI[uri] != count {
			t.Fatalf("occurrences by URI = %+v, want %q count %d", byURI, uri, count)
		}
	}

	// A newly-created, unsaved package file still belongs to the directory
	// package and must participate in a rename.
	unsavedPath := filepath.Join(root, "math", "new.toy")
	server.docs.open(testFileURI(unsavedPath), "'math package\n'double-again [ double ] def\n", 1)
	occurrences = server.wordOccurrences(target, true)
	if len(occurrences) != len(want)+1 {
		t.Fatalf("occurrences with unsaved package file = %+v", occurrences)
	}
}

func TestCaptureRenamePreservesFetchPrefix(t *testing.T) {
	root := t.TempDir()
	path := writeToyFile(t, root, "capture.toy", "'read [ | value | $value ] def\n")
	server := NewServer(log.New(io.Discard, "", 0))
	server.docs.configureWorkspace(initializeParams{RootURI: testFileURI(root)})

	params, err := json.Marshal(renameParams{
		TextDocument: textDocumentIdentifier{URI: testFileURI(path)},
		Position:     lspPosition{Line: 0, Character: 11},
		NewName:      "item",
	})
	if err != nil {
		t.Fatal(err)
	}
	var output bytes.Buffer
	if err := server.handleRename(&output, request{ID: json.RawMessage("1"), Params: params}); err != nil {
		t.Fatal(err)
	}
	response := decodeTestResponse(t, output.Bytes())
	var edit workspaceEdit
	if err := json.Unmarshal(response.Result, &edit); err != nil {
		t.Fatal(err)
	}
	edits := edit.Changes[testFileURI(path)]
	if len(edits) != 2 {
		t.Fatalf("capture rename edits = %+v", edits)
	}
	if edits[0].Range.Start.Character != 10 || edits[1].Range.Start.Character != 19 {
		t.Fatalf("capture rename ranges include syntax prefixes: %+v", edits)
	}
	for _, change := range edits {
		if change.NewText != "item" {
			t.Fatalf("capture rename text = %+v", change)
		}
	}
}

func TestHoverUsesCorePackageWordMetadata(t *testing.T) {
	root := t.TempDir()
	path := writeToyFile(t, root, "main.toy", `'main package
"core:ffi" 'c import-as
c.bind`)
	server := NewServer(log.New(io.Discard, "", 0))
	server.docs.configureWorkspace(initializeParams{RootURI: testFileURI(root)})

	hover, ok := server.resolveCorePackageHover(
		testFileURI(path), analysis.Position{Line: 2, Character: 3})
	if !ok {
		t.Fatal("core package hover did not resolve")
	}
	want := "```toy\nc.bind\n```\nstack: `library symbol signature -- function`\n\n" +
		"Resolve a library symbol using an input-to-output signature such as `cstr -> usize`."
	if hover.Contents != want {
		t.Fatalf("core package hover = %q, want %q", hover.Contents, want)
	}
}

func TestHoverUsesSourceCorePackageMetadata(t *testing.T) {
	root := t.TempDir()
	path := writeToyFile(t, root, "main.toy", `'main package
"core:json" import
json.decode`)
	server := NewServer(log.New(io.Discard, "", 0))
	server.docs.configureWorkspace(initializeParams{RootURI: testFileURI(root)})

	hover, ok := server.resolveCorePackageHover(
		testFileURI(path), analysis.Position{Line: 2, Character: 7})
	if !ok {
		t.Fatal("source core package hover did not resolve")
	}
	want := "```toy\njson.decode\n```\nstack: `string -- value`\n\n" +
		"Decode one strict UTF-8 JSON document or raise an error."
	if hover.Contents != want {
		t.Fatalf("source core package hover = %q, want %q", hover.Contents, want)
	}
}

func TestSafeCorePackagePath(t *testing.T) {
	tests := map[string]bool{
		"ffi":           true,
		"text/format":   true,
		"text\\format":  true,
		"":              false,
		"../ffi":        false,
		"text/./format": false,
		"text//format":  false,
		"other:ffi":     false,
	}
	for path, want := range tests {
		if got := safeCorePackagePath(path); got != want {
			t.Errorf("safeCorePackagePath(%q) = %v, want %v", path, got, want)
		}
	}
}

func writeToyFile(t *testing.T, root, relative, contents string) string {
	t.Helper()
	path := filepath.Join(root, relative)
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, []byte(contents), 0o644); err != nil {
		t.Fatal(err)
	}
	return path
}
