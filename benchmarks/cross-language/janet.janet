(defn inc [value]
  (+ value 1))

(defn arithmetic []
  (var value 0)
  (for index 0 10000000
    (set value (+ value 1)))
  value)

(defn dispatch []
  (var value 0)
  (for index 0 10000000
    (set value (inc value)))
  value)

(defn fib [value]
  (if (< value 2)
    value
    (+ (fib (- value 1)) (fib (- value 2)))))

(defn sequence []
  (var total 0)
  (for pass 0 10
    (let [values @[]]
      (for index 0 20000
        (array/push values 1))
      (each value values
        (set total (+ total value)))))
  total)

(defn string-build []
  (var total 0)
  (for pass 0 20
    (let [value @""]
      (for index 0 10000
        (buffer/push-string value "x"))
      (set total (+ total (length value)))))
  total)

(defn map-lookup []
  (let [values @{}]
    (for key 0 50000
      (put values key (* key 3)))
    (var total 0)
    (for pass 0 20
      (for key 0 50000
        (set total (+ total (get values key)))))
    total))

(def benchmark
  (case ((dyn :args) 1)
    "arithmetic" arithmetic
    "dispatch" dispatch
    "fibonacci" (fn [] (fib 32))
    "sequence" sequence
    "string" string-build
    "map-lookup" map-lookup
    (error "usage: janet.janet <benchmark>")))

(def started (os/clock :cputime))
(def result (benchmark))
(def elapsed (- (os/clock :cputime) started))
(print result)
(print elapsed)
