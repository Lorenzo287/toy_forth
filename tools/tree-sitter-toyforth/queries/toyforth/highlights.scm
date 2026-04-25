; @keyword: Usually Purple/Pink. (Commonly used for def, return).
; @keyword.control: Usually Purple/Pink (often italicized). (For if, while).
; @string.special: Usually Cyan/Light Blue. (Used for escape characters or regex).
; @constant: Usually Orange/Red. (For hardcoded constants).
; @constant.builtin: Often Red. (For values like nil, null, true, false).
; @type: Usually Yellow/Aqua. (For class names or types).
; @type.builtin: Often Yellow. (For int, float, string).
; @tag: Usually Red/Pink/Green. (From HTML, but very distinct in most themes).
; @label: Usually Blue/Cyan. (Used for loop labels or jump targets).
; @namespace: Usually Yellow/Aqua. (For modules or packages).
; @attribute: Usually Blue/Cyan. (Used for decorators/annotations).
; @comment.note: Often Cyan/Blue (bold).
; @comment.error: Often Red (bold).
; @punctuation.special: Usually Red/Orange.

(line_comment) @comment
(block_comment) @comment

(number) @number
(boolean) @boolean
(string) @string
(escape_sequence) @string.escape

;; Quoted symbols
(quoted_symbol "'" @punctuation.delimiter (symbol_name) @function)

;; Variables and Parameters
(var_list (word) @variable)
(var_fetch (variable_name) @variable)

;; Braces and Fetch Symbol
(var_fetch "$" @type)
(var_list "{" @type "}" @type)

;; Brackets
(block "[" @punctuation.bracket "]" @punctuation.bracket)

(colon_definition
  ":" @punctuation.delimiter
  (definition_name) @function
  ";" @punctuation.delimiter)

(control_word) @keyword.control
(operator) @operator
(builtin_word) @function.builtin

;; Word 'def' specifically as keyword
((builtin_word) @keyword
 (#eq? @keyword "def"))

;; Generic words / User calls
(word) @variable

