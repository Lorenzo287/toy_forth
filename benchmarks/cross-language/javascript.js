function inc(value) {
    return value + 1;
}

function arithmetic() {
    let value = 0;
    for (let index = 0; index < 10_000_000; index++) {
        value += 1;
    }
    return value;
}

function dispatch() {
    let value = 0;
    for (let index = 0; index < 10_000_000; index++) {
        value = inc(value);
    }
    return value;
}

function fib(value) {
    if (value < 2) return value;
    return fib(value - 1) + fib(value - 2);
}

function sequence() {
    let total = 0;
    for (let pass = 0; pass < 10; pass++) {
        const values = [];
        for (let index = 0; index < 20_000; index++) {
            values.push(1);
        }
        for (let index = 0; index < values.length; index++) {
            total += values[index];
        }
    }
    return total;
}

function stringBuild() {
    let total = 0;
    for (let pass = 0; pass < 20; pass++) {
        const parts = [];
        for (let index = 0; index < 10_000; index++) {
            parts.push("x");
        }
        total += parts.join("").length;
    }
    return total;
}

function mapLookup() {
    const values = new Map();
    for (let key = 0; key < 50_000; key++) {
        values.set(key, key * 3);
    }
    let total = 0;
    for (let pass = 0; pass < 20; pass++) {
        for (let key = 0; key < 50_000; key++) {
            total += values.get(key);
        }
    }
    return total;
}

const benchmarks = {
    arithmetic,
    dispatch,
    fibonacci: () => fib(32),
    sequence,
    string: stringBuild,
    "map-lookup": mapLookup,
};

const benchmark = benchmarks[process.argv[2]];
if (benchmark === undefined) {
    throw new Error("usage: javascript.js <benchmark>");
}

const started = process.cpuUsage();
const result = benchmark();
const elapsed = process.cpuUsage(started);
console.log(result);
console.log(elapsed.user + elapsed.system);
