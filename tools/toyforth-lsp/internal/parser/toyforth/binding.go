package toyforth

/*
#cgo CFLAGS: -I../../../../tree-sitter-toyforth/src
#include "../../../../tree-sitter-toyforth/src/parser.c"
*/
import "C"

import (
	"unsafe"

	tree_sitter "github.com/tree-sitter/go-tree-sitter"
)

func Language() *tree_sitter.Language {
	return tree_sitter.NewLanguage(unsafe.Pointer(C.tree_sitter_toyforth()))
}
