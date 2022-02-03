#!/bin/bash

gcc -o vim.so vim.c $(yed --print-cflags) $(yed --print-ldflags)
