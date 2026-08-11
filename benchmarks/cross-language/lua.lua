local function inc(value)
    return value + 1
end

local function arithmetic()
    local value = 0
    for _ = 1, 10000000 do
        value = value + 1
    end
    return value
end

local function dispatch()
    local value = 0
    for _ = 1, 10000000 do
        value = inc(value)
    end
    return value
end

local function fib(value)
    if value < 2 then
        return value
    end
    return fib(value - 1) + fib(value - 2)
end

local function sequence()
    local total = 0
    for _ = 1, 10 do
        local values = {}
        for _ = 1, 20000 do
            values[#values + 1] = 1
        end
        for index = 1, #values do
            total = total + values[index]
        end
    end
    return total
end

local function string_build()
    local total = 0
    for _ = 1, 20 do
        local parts = {}
        for index = 1, 10000 do
            parts[index] = "x"
        end
        total = total + #table.concat(parts)
    end
    return total
end

local function map_lookup()
    local values = {}
    for key = 0, 49999 do
        values[key] = key * 3
    end
    local total = 0
    for _ = 1, 20 do
        for key = 0, 49999 do
            total = total + values[key]
        end
    end
    return total
end

local benchmarks = {
    arithmetic = arithmetic,
    dispatch = dispatch,
    fibonacci = function() return fib(32) end,
    sequence = sequence,
    string = string_build,
    ["map-lookup"] = map_lookup,
}

local benchmark = benchmarks[arg[1]]
if benchmark == nil then
    error("usage: lua.lua <benchmark>")
end

local started = os.clock()
local result = benchmark()
local elapsed = math.floor((os.clock() - started) * 1000000000 + 0.5)
print(result)
print(elapsed)
