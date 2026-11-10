// Symbols that rAthena's server binaries get from core.cpp (which also needs each server's do_init/do_final).
#include <common/core.hpp>

char* SERVER_NAME = const_cast<char*>( "fuzz" );

const char* get_git_hash( void ) {
	return "fuzz";
}

const char* get_svn_revision( void ) {
	return "fuzz";
}
