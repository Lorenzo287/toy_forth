package analysis

import "strings"

type Hover struct {
	Contents string
	Range    Range
}

type builtinDoc struct {
	Signature   string
	Description string
}

var builtinDocs = map[string]builtinDoc{
	"dup":    {Signature: "dup ( x -- x x )", Description: "Duplicate the top stack item."},
	"drop":   {Signature: "drop ( x -- )", Description: "Discard the top stack item."},
	"swap":   {Signature: "swap ( a b -- b a )", Description: "Swap the top two stack items."},
	"over":   {Signature: "over ( a b -- a b a )", Description: "Copy the second stack item to the top."},
	"rot":    {Signature: "rot ( a b c -- b c a )", Description: "Rotate the top three stack items."},
	"+":      {Signature: "+ ( a b -- a+b )", Description: "Add two numeric values."},
	"-":      {Signature: "- ( a b -- a-b )", Description: "Subtract the top value from the next value."},
	"*":      {Signature: "* ( a b -- a*b )", Description: "Multiply two numeric values."},
	"/":      {Signature: "/ ( a b -- a/b )", Description: "Divide the next value by the top value."},
	"%":      {Signature: "% ( a b -- a%b )", Description: "Compute the remainder of integer division."},
	"mod":    {Signature: "mod ( a b -- a%b )", Description: "Compute the modulo of two values."},
	"abs":    {Signature: "abs ( x -- |x| )", Description: "Replace a number with its absolute value."},
	"neg":    {Signature: "neg ( x -- -x )", Description: "Negate the top numeric value."},
	"max":    {Signature: "max ( a b -- max )", Description: "Keep the larger of two values."},
	"min":    {Signature: "min ( a b -- min )", Description: "Keep the smaller of two values."},
	"==":     {Signature: "== ( a b -- bool )", Description: "Compare two values for equality."},
	"!=":     {Signature: "!= ( a b -- bool )", Description: "Compare two values for inequality."},
	"<":      {Signature: "< ( a b -- bool )", Description: "Check whether the next value is less than the top value."},
	">":      {Signature: "> ( a b -- bool )", Description: "Check whether the next value is greater than the top value."},
	"<=":     {Signature: "<= ( a b -- bool )", Description: "Check whether the next value is less than or equal to the top value."},
	">=":     {Signature: ">= ( a b -- bool )", Description: "Check whether the next value is greater than or equal to the top value."},
	"if":     {Signature: "if ( cond block -- )", Description: "Execute the block when the condition is truthy."},
	"ifelse": {Signature: "ifelse ( cond then else -- )", Description: "Execute one of two blocks based on the condition."},
	"while":  {Signature: "while ( cond body -- )", Description: "Repeat while the condition block yields a truthy value."},
	"times":  {Signature: "times ( n block -- )", Description: "Execute a block a fixed number of times."},
	"each":   {Signature: "each ( list block -- )", Description: "Execute a block for each item in a list."},
	"exec":   {Signature: "exec ( block -- ... )", Description: "Execute a quoted block."},
	"print":  {Signature: "print ( x -- )", Description: "Print a value."},
	"printf": {Signature: "printf ( x -- )", Description: "Print a value without the default formatting used by print."},
	".":      {Signature: ". ( x -- )", Description: "Print the top stack item."},
	".s":     {Signature: ".s ( -- )", Description: "Print the current stack state."},
	"key":    {Signature: "key ( -- x )", Description: "Read a key from input."},
	"input":  {Signature: "input ( -- x )", Description: "Read input from stdin."},
	"geth":   {Signature: "geth ( list idx -- value )", Description: "Fetch a list element by index."},
	"seth":   {Signature: "seth ( list idx value -- list )", Description: "Store a list element by index."},
	"len":    {Signature: "len ( x -- n )", Description: "Get the length of a list or string."},
	"rand":   {Signature: "rand ( -- n )", Description: "Push a random integer."},
	"sleep":  {Signature: "sleep ( ms -- )", Description: "Pause execution for a number of milliseconds."},
	"time":   {Signature: "time ( -- n )", Description: "Push the current time value."},
	"exit":   {Signature: "exit ( code -- )", Description: "Terminate the interpreter."},
	"def":    {Signature: "def ( 'name block -- )", Description: "Bind a quoted symbol to a block definition."},
}

func LookupHover(index DocumentIndex, pos Position) (Hover, bool) {
	word, tok, ok := lookupTokenAt(index, pos)
	if !ok {
		return Hover{}, false
	}

	if tok.Kind == tokenKindVariable {
		local, ok := lookupLocalBinding(index, word, pos)
		if !ok {
			return Hover{}, false
		}
		return Hover{
			Contents: "```toyforth\n$" + local.Name + "\n```\nLocal binding from `{ " + local.Name + " }`.",
			Range:    tok.Range,
		}, true
	}

	if doc, ok := builtinDocs[word]; ok {
		return Hover{
			Contents: "```toyforth\n" + doc.Signature + "\n```\n" + doc.Description,
			Range:    tok.Range,
		}, true
	}

	if sym, ok := index.Definitions[word]; ok {
		header := sym.Name
		if sym.StackEffect != "" {
			header += " ( " + sym.StackEffect + " )"
		}

		body := sym.Detail
		switch {
		case sym.Doc != "" && sym.StackEffect != "":
			body = sym.Doc + "\n\nStack effect: `" + sym.StackEffect + "`"
		case sym.Doc != "":
			body = sym.Doc
		case sym.StackEffect != "":
			body = sym.StackEffect
		}

		return Hover{
			Contents: strings.TrimSpace("```toyforth\n" + header + "\n```\n" + body),
			Range:    tok.Range,
		}, true
	}

	return Hover{}, false
}
