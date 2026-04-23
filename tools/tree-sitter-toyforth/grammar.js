/**
 * @file Toyforth grammar for tree-sitter
 * @author Lorenzo287
 * @license MIT
 */

export default grammar({
  name: 'toyforth',
  extras: $ => [
    /\s/,
  ],
  rules: {
    source_file: $ => repeat(choice(
      $._expression,
      $.line_comment,
      $.block_comment,
    )),

    _expression: $ => choice(
      $.number,
      $.boolean,
      $.string,
      $.quoted_symbol,
      $.var_fetch,
      $.block,
      $.var_list,
      $.colon_definition,
      $.control_word,
      $.operator,
      $.builtin_word,
      $.word,
    ),

    line_comment: $ => token(seq('\\', /.*/)),
    block_comment: $ => token(seq('(', /[^)]*/, ')')),
    boolean: $ => choice('true', 'false'),
    number: $ => token(choice(
      /\d+\.\d+/,  // float first
      /\d+/,
    )),
    control_word: $ => choice(
      'if', 'ifelse', 'while', 'times', 'each', 'exec'
    ),
    operator: $ => choice(
      '+', '-', '*', '/', '%', 'mod', 'abs', 'neg', 'max', 'min',
      '==', '!=', '<', '>', '<=', '>='
    ),
    builtin_word: $ => choice(
      'dup', 'drop', 'swap', 'over', 'rot',
      'print', 'printf', '.', '.s',
      'key', 'input', 'geth', 'seth', 'len', 'rand', 'sleep', 'time', 'exit',
      'def'
    ),
    string: $ => seq(
      '"',
      repeat(choice(
        $._string_content,
        $.escape_sequence,
      )),
      '"',
    ),
    _string_content: $ => token.immediate(/[^"\\]+/),
    escape_sequence: $ => token.immediate(choice(
      /\\n/,
      /\\r/,
      /\\t/,
      /\\"/,
      /\\\\/,
      /\\033/,
      /\\0/,
      /\\./,  // catch-all
    )),
    quoted_symbol: $ => seq(
      "'",
      alias(/[a-zA-Z0-9_+\-*/%<>=!:;.]+/, $.symbol_name)
    ),
    var_fetch: $ => seq(
      '$',
      alias(/[a-zA-Z0-9_+\-*/%<>=!:;.]+/, $.variable_name)
    ),
    block: $ => seq(
      '[',
      repeat(choice($._expression, $.line_comment, $.block_comment)),
      ']',
    ),
    // fix 1: allow any expression inside {}, mirroring the lexer
    var_list: $ => seq(
      '{',
      repeat(choice($._expression, $.line_comment, $.block_comment)),
      '}',
    ),
    // fix 2: use dedicated _def_name rule instead of aliasing $.word
    colon_definition: $ => seq(
      ':',
      alias($._def_name, $.definition_name),
      repeat(choice($._expression, $.line_comment, $.block_comment)),
      ';',
    ),
    _def_name: $ => token(/[a-zA-Z0-9_+\-*/%<>=!:;.]+/),

    word: $ => /[a-zA-Z0-9_+\-*/%<>=!:;.]+/,
  }
});
