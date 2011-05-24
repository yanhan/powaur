#!/bin/sh -xu

[ -f Makefile ] && make distclean
rm -rf autom4te.cache
rm -f config.h
rm -f config.h.in
rm -f config.log
rm -f config.status
rm -f configure
rm -f Makefile
