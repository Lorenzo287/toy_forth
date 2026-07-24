#lang racket
; racket examples\dynamic_prog\top-down.rkt

(define (fib n)
  (define cache (make-hash '((0 . 0) (1 . 1))))

  (define (go k)
    (hash-ref! cache k (lambda () (+ (go (sub1 k)) (go (- k 2))))))

  (go n))

(print (fib 35))
