(block
  "[" @indent.begin
  "]" @indent.end)

(colon_definition
  ":" @indent.begin
  ";" @indent.end)

(var_list
  "{" @indent.begin
  "}" @indent.end)
