(defn fib-memo [memo n]
  (if (contains? memo n)
    [memo (get memo n)]
    (let [[memo1 a] (fib-memo memo  (dec n))
          [memo2 b] (fib-memo memo1 (- n 2))
          value     (+ a b)]
      [(assoc memo2 n value) value])))

(defn fib-top [n]
  (second (fib-memo {0 0, 1 1} n)))

(print (fib-top 35))
