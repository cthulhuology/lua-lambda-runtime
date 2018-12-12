
.PHONY: curl 

curl:
	cd curl && ./buildconf && ./configure --prefix=${HOME}/lua-lambda-runtime/ --without-ssl --without-libssh2 --disable-dict --disable-file --disable-ftp --disable-ftps --disable-gopher --disable-imap --disable-imap --disable-imaps --disable-ldap --disable-ldaps --disable-pop3 --disable-pop3s --disable-rtsp --disable-smtp --disable-smtps --disable-telnet --disable-tftp --disable-verbose --disable-shared --enable-static --disable-manual --disable-debug && make && make install-strip

