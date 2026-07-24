#lang racket
; racket examples\dynamic_prog\bottom-up.rkt

(define (fib n)
  (let loop ([remaining n]
             [a 0]
             [b 1])
    (if (zero? remaining)
        a
        (loop (sub1 remaining) b (+ a b)))))

(print (fib 35))
