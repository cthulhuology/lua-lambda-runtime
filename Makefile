
.PHONY: clean test

all: lambda

BASEDIR=$(shell pwd)

lib/libcurl.a: 
	cd curl && CFLAGS=-I$(BASEDIR)/mbedtls/include LDFLAGS=-L$(BASEDIR)/mbedtls/library ./configure --prefix=$(BASEDIR)/ --with-mbedtls=$(BASEDIR)/mbedtls --without-ssl --without-libssh2 --disable-dict --disable-file --disable-ftp --disable-ftps --disable-gopher --disable-imap --disable-imap --disable-imaps --disable-ldap --disable-ldaps --disable-pop3 --disable-pop3s --disable-rtsp --disable-smtp --disable-smtps --disable-telnet --disable-tftp --disable-verbose --disable-shared --enable-static --disable-manual --disable-debug && make && make install-strip

lib/luajit.a:
	cd luajit-2.0 && make
	cp luajit-2.0/src/libluajit.a lib/

lib/mbedtls.a:
	cd mbedtls && git checkout mbedtls-2.15.1 && make no_test
	for I in $$(ls mbedtls/library/* | grep "a$$"); do cp $$I lib/; done
	
bootstrap: src/bootstrap.c lib/libluajit.a lib/libmbedtls.a lib/libcurl.a
	gcc -o bootstrap src/bootstrap.c -Llib -Imbedtls/include -Icurl/include/ -Iluajit-2.0/src/ -lcurl -lluajit -lmbedtls -lmbedcrypto -lmbedx509 -lz -lpthread -lm -ldl

test: bootstrap
	_HANDLER=index.handler AWS_LAMBDA_RUNTIME_API=localhost:9001 ./bootstrap

lua.zip: bootstrap
	zip lua.zip bootstrap 

lua-layer: lua.zip
	aws lambda publish-layer-version --layer-name lua-runtime --zip-file fileb://lua.zip | \
		jq -r '.["LayerVersionArn"]' > LATESTARN
	cat LATESTARN

update: lua-layer
	aws lambda update-function-configuration --function-name test-lua-runtime --layers $$(cat LATESTARN)

test-lua:
	zip test-lua.zip test.lua
	aws lambda create-function --function-name test-lua-runtime \
		--zip-file fileb://test-lua.zip --handler test.handler \
		--runtime provided \
		--layers $$(cat LATESTARN) \
		--role arn:aws:iam::097591811552:role/lambda-role

clean:
	rm bootstrap 
	rm lua.zip
	aws lambda delete-function --function-name test-lua-runtime
