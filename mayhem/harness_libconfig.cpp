// Fuzzes the bundled libconfig parser through config_read_string (how rAthena reads conf/*.conf),
// then walks the setting tree and round-trips it through config_write into a memory stream.
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <libconfig.h>

static void walk( const config_setting_t* s, int depth ) {
	if( s == nullptr || depth > 64 ){
		return;
	}

	config_setting_name( s );
	config_setting_is_group( s );

	switch( config_setting_type( s ) ){
		case CONFIG_TYPE_INT: config_setting_get_int( s ); break;
		case CONFIG_TYPE_INT64: config_setting_get_int64( s ); break;
		case CONFIG_TYPE_FLOAT: config_setting_get_float( s ); break;
		case CONFIG_TYPE_BOOL: config_setting_get_bool( s ); break;
		case CONFIG_TYPE_STRING: config_setting_get_string( s ); break;
		default: break;
	}

	int n = config_setting_length( s );

	for( int i = 0; i < n; i++ ){
		walk( config_setting_get_elem( s, i ), depth + 1 );
	}
}

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size ) {
	if( memmem( data, size, "@include", 8 ) != nullptr ){
		return -1;
	}

	std::string buf( reinterpret_cast<const char*>( data ), size );
	config_t config;

	config_init( &config );

	if( !config_read_string( &config, buf.c_str() ) ){
		config_error_line( &config );
		config_error_text( &config );
		config_destroy( &config );
		return -1;
	}

	walk( config_root_setting( &config ), 0 );

	int ival;
	const char* sval;
	config_lookup_int( &config, "a.b", &ival );
	config_lookup_string( &config, "name", &sval );

	char* out = nullptr;
	size_t outlen = 0;
	FILE* mem = open_memstream( &out, &outlen );

	if( mem != nullptr ){
		config_write( &config, mem );
		fclose( mem );
		free( out );
	}

	config_destroy( &config );
	return 0;
}
