local M = {}

function M.setup()
    local parser_config = require("nvim-treesitter.parsers").get_parser_configs()
    
    -- Get the directory of this file to determine the base path
    -- This makes it portable regardless of where the repo is cloned
    local str = debug.getinfo(1, "S").source:sub(2)
    local base_path = str:match("(.*[/\\])lua[/\\]toyforth[/\\]init%.lua$")
    
    if not base_path then
        -- Fallback to a reasonable default if path detection fails
        base_path = vim.fn.stdpath("config") .. "/site/pack/packer/start/toy_forth/tools/tree-sitter-toyforth/"
    end

    parser_config.toyforth = {
        install_info = {
            url = base_path, -- Use the detected local path
            files = { "src/parser.c" },
            branch = "main",
        },
        filetype = "toyforth",
    }

    vim.filetype.add({
        extension = {
            fth = "toyforth",
            tf = "toyforth",
        },
    })
end

return M
