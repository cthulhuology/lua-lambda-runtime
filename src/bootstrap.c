// bootstrap.c
// 
// This is a LuaJit runtime for AWS Lambda
//

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <curl/curl.h>
#include <mbedtls/net.h>
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/debug.h>

char* bootstrap_error_messages[] = {
	"",
	"Missing endpoint please set AWS_LAMBDA_RUNTIME_API environment variable",
	"Missing handler please set _HANDLER environment variable",
	"Missing task root please set LAMBDA_TASK_ROOT environment variable",
	"Failed to initialize lua",
	"Failed to initialize curl",
	"Failed to get a job",
	"Failed to handle the request",
	"Failed to post the response"
};

enum bootstrap_errors {
	NONE,
	BOOTSTRAP_ENDPOINT_MISSING,
	BOOTSTRAP_HANDLER_MISSING,
	BOOTSTRAP_ROOT_MISSING,
	BOOTSTRAP_INIT_LUA,
	BOOTSTRAP_INIT_CURL,
	BOOTSTRAP_GET_JOB,
	BOOTSTRAP_HANDLE_REQUEST,
	BOOTSTRAP_POST_RESPONSE,
};

void die(int error) {
	fprintf(stderr,"%s\n",bootstrap_error_messages[error]);
	exit(error);
}

void error (lua_State *L, const char *fmt, ...) {
	va_list argp;
	va_start(argp, fmt);
	vfprintf(stderr, fmt, argp);
	va_end(argp);
	lua_close(L);
	die(BOOTSTRAP_HANDLE_REQUEST);
}

static int traceback (lua_State *L) {
	lua_getfield(L, LUA_GLOBALSINDEX, "debug");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return 1;
	}
	lua_getfield(L, -1, "traceback");
	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 2);
		return 1;
	}
	lua_pushvalue(L, 1);  /* pass error message */
	lua_pushinteger(L, 2);  /* skip this function and traceback */
	lua_call(L, 2, 1);  /* call debug.traceback */
	return 1;
}

size_t job_header_callback(char *buffer, size_t size, size_t nitems, lua_State *L) {
	int i;
	// TODO parse headers and set the right shit
	for (i = 0; i < size*nitems && buffer[i] != ':'; ++i);
	if (i >= size*nitems) return size*nitems;
	lua_getglobal(L,"context");			// get the context object
	lua_pushlstring(L,buffer,i);			// key
	lua_pushlstring(L,buffer+i+2,size*nitems-i-4);	// value
	lua_settable(L,-3);				// t[key] = value
	lua_pop(L,1);					// remove the context object from the stack
	return size*nitems;
}

size_t job_write_callback(char *ptr, size_t size, size_t nmemb, lua_State *L) {
	lua_getglobal(L,"message");		// get the global message string
	lua_pushlstring(L,ptr,nmemb*size);	// push the new data
	lua_concat(L,2);			// concat them
	lua_setglobal(L,"message");		// set the global message string
	return size*nmemb;
}
 

const char* get_job(lua_State *L, const char *endpoint) {
	CURL *curl = curl_easy_init();
	CURLcode res;
	char* url =  NULL;
	if (0 > asprintf(&url,"http://%s/2018-06-01/runtime/invocation/next", endpoint)) {
		fprintf(stderr,"Failed to construct url\n");
		die(BOOTSTRAP_GET_JOB);
	}
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, job_header_callback); 
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, L); 
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, job_write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, L);
	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		fprintf(stderr,"error: %s",curl_easy_strerror(res));
		die(BOOTSTRAP_GET_JOB);
	}
	curl_easy_cleanup(curl);
	return "abc";
}

size_t response_header_callback(char *buffer, size_t size, size_t nitems, lua_State *L) {
	return size;
}

size_t response_write_callback(char *ptr, size_t size, size_t nmemb, lua_State *L) {
	return size;
}
 
int post_response(lua_State *L, const char *endpoint, const char *requestId, const char *response) {
	CURL *curl = curl_easy_init();
	CURLcode res;
	char* url =  NULL;
	if (0 > asprintf(&url,"http://%s/2018-06-01/runtime/invocation/%s/response", endpoint,requestId)) {
		fprintf(stderr,"Failed to construct url\n");
		die(BOOTSTRAP_POST_RESPONSE);
	}
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, response);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(response));
	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		fprintf(stderr,"error: %s",curl_easy_strerror(res));
		die(BOOTSTRAP_POST_RESPONSE);
	}
	curl_easy_cleanup(curl);
	return 0;
}

void handle_request(const char* endpoint,const char* module, const char* function) {
	const char *response, *err, *requestId;
	lua_State *L = luaL_newstate();
	if (!L) die(BOOTSTRAP_INIT_LUA);
	luaL_openlibs(L);
	if (luaL_dofile(L,module)) {
		fprintf(stderr,"Failed to load module %s\n", module);
		die(BOOTSTRAP_INIT_LUA);
	}
	lua_newtable(L);		// create an empty context object
	lua_setglobal(L,"context");
	lua_pushstring(L,"");		// the message starts as an empty string
	lua_setglobal(L,"message");
	get_job(L,endpoint);
	lua_getglobal(L,function);
	lua_getglobal(L,"message");	// module.function(message,context)
	lua_getglobal(L,"context");
	if (lua_pcall(L, 2, 2, 0)) {
		err = lua_tostring(L,-1);
		fprintf(stderr,"Lua error: %s\n", err);
		traceback(L);
		return;
	}
	response = lua_tostring(L,-2);	// returns (response,error)
	err = lua_tostring(L,-1);
	lua_pop(L,2);
	lua_getglobal(L,"context");
	lua_pushstring(L,"Lambda-Runtime-Aws-Request-Id");
	lua_gettable(L,-2);
	requestId = lua_tostring(L,-1);	
	post_response(L,endpoint,requestId,response);
	lua_close(L);
}

int main(int argc, char** argv) {
	const char *requestId;
	const char *endpoint = getenv("AWS_LAMBDA_RUNTIME_API");
	const char *handler = getenv("_HANDLER");
	const char *where = getenv("LAMBDA_TASK_ROOT");
	char *module = NULL;
	char *function = NULL;
	int i,l;
	if (!endpoint) die(BOOTSTRAP_ENDPOINT_MISSING);
	if (!handler) die(BOOTSTRAP_HANDLER_MISSING);
	if (!where) die(BOOTSTRAP_ROOT_MISSING);
	chdir(where);
	l = strlen(handler);
	for (i = 0; i < l && handler[i] != '.'; ++i);
	asprintf(&module, "%.*s.lua",i,handler);
	asprintf(&function, "%.*s",l-i-1,handler+i+1);
	for (;;) handle_request(endpoint,module,function);
	return 0; 	// never get here
}
