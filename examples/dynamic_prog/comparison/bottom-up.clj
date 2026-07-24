(defn fib-bottom [n]
  (loop [i 0
         a 0
         b 1]
    (if (= i n)
      a
      (recur (inc i) b (+ a b)))))

(print (fib-bottom 35))
