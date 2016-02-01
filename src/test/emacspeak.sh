#!/bin/sh

cd ${EMACSPEAK_INSTALL}
cd ./share/emacs/site-lisp/emacspeak/servers/linux-outloud/
cp Makefile Makefile.orig
sed -i 's/-m32//' Makefile
make all

emacs -q -l ${EMACSPEAK_DIR}/emacspeak-setup.el -l $HOME/.emacs

