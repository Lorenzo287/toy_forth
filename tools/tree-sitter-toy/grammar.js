/**
 * @file Toy grammar for tree-sitter
 * @author lorenzotumini
 * @license MIT
 */

import { builtinWords, controlWords, operatorWords } from './builtin-words.js';

export default grammar({
  name: 'toy',
  extras: $ => [
    /\s/,
  ],
  rules: {
    source_file: $ => repeat(choice(
      $._expression,
      $._comment,
    )),

    _expression: $ => choice(
      $._non_capture_expression,
      $.var_list,
    ),

    _non_capture_expression: $ => choice(
      $.number,
      $.boolean,
      $.string,
      $.quoted_symbol,
      $.var_fetch,
      $.block,
      $.list_literal,
      $.map_literal,
      $.set_literal,
      $.control_word,
      $.operator,
      $.builtin_word,
      $.word,
    ),

    _comment: $ => choice($.line_comment, $.block_comment),
    line_comment: $ => token(seq('\\', /.*/)),
    block_comment: $ => token(seq('/*', /[^*]*\*+([^/*][^*]*\*+)*/, '/')),
    boolean: $ => choice('true', 'false'),
    number: $ => token(/[+-]?\d+(?:\.\d*)?(?:[eE][+-]?\d+)?/),
    control_word: $ => choice(...controlWords),
    operator: $ => choice(...operatorWords),
    builtin_word: $ => choice(...builtinWords),
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
      /\\x[0-9a-fA-F]{2}/,
    )),
    quoted_symbol: $ => seq(
      "'",
      alias(choice('/', /\.(?:s|S)?/, /[a-zA-Z0-9_+\-*%<>=!?]+(?:\.[a-zA-Z0-9_+\-*%<>=!?]+)*/), $.symbol_name)
    ),
    var_fetch: $ => seq(
      '$',
      alias(/[a-zA-Z0-9_+\-*%<>=!?]+/, $.variable_name)
    ),
    block: $ => seq(
      '[',
      repeat(choice($._expression, $._comment)),
      ']',
    ),
    list_literal: $ => seq(
      '(',
      repeat(choice($._expression, $._comment)),
      ')',
    ),
    map_literal: $ => seq(
      '{',
      repeat(choice($._expression, $._comment)),
      '}',
    ),
    set_literal: $ => seq(
      '#{',
      repeat(choice($._expression, $._comment)),
      '}',
    ),
    // Captures bind names from the data stack inside the current frame.
    var_list: $ => seq(
      '|',
      repeat(choice($._non_capture_expression, $._comment)),
      '|',
    ),
    word: $ => /[a-zA-Z_+\-*%<>=!?][a-zA-Z0-9_+\-*%<>=!?]*(?:\.[a-zA-Z_+\-*%<>=!?][a-zA-Z0-9_+\-*%<>=!?]*)*/,
  }
});
