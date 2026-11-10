// Known-answer probe for mayhem/test.sh: runs rAthena's strlib, bundled libconfig and YamlDatabase
// on fixture files and prints what they parsed, for comparison against kat/*.expected.
// usage: rathena-kat sv|unescape|conf|yaml <file>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <libconfig.h>

#include <common/database.hpp>
#include <common/showmsg.hpp>
#include <common/strlib.hpp>

extern void do_init_database();

static std::string slurp( const char* path ) {
	std::ifstream in( path, std::ios::binary );
	return std::string( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
}

static int run_sv( const std::string& text ) {
	std::string buf = text;
	size_t off = 0;
	int line = 0;

	while( off < buf.size() ){
		char* fields[6];
		bool error = false;
		size_t n = sv_split( &buf[0], buf.size(), off, ',', fields, 6, SV_ESCAPE_C | SV_TERMINATE_LF | SV_TERMINATE_CRLF, error );

		if( error || n == (size_t)-1 ){
			printf( "line %d: error\n", line );
			return 0;
		}

		printf( "line %d: %zu fields", line, n );

		for( size_t i = 1; i <= n && i < 6; i++ ){
			printf( " [%s]", fields[i] );
		}

		printf( "\n" );
		off = fields[0] - buf.data();
		line++;
	}

	return 0;
}

static int run_unescape( const std::string& text ) {
	std::string out( text.size() + 1, '\0' );
	size_t n = sv_unescape_c( &out[0], text.c_str(), text.size() );
	printf( "unescaped %zu bytes:", n );

	for( size_t i = 0; i < n; i++ ){
		printf( " %02x", (unsigned char)out[i] );
	}

	std::string esc( n * 4 + 1, '\0' );
	size_t m = sv_escape_c( &esc[0], out.c_str(), n, SV_ESCAPE_C_SUPPORTED );
	printf( "\nescaped %zu bytes: %s\n", m, esc.c_str() );
	return 0;
}

static int run_conf( const std::string& text ) {
	config_t config;
	config_init( &config );

	if( !config_read_string( &config, text.c_str() ) ){
		printf( "parse error at line %d: %s\n", config_error_line( &config ), config_error_text( &config ) );
		config_destroy( &config );
		return 0;
	}

	int ival = -1;
	const char* sval = nullptr;
	double dval = -1;
	int bval = -1;

	if( config_lookup_int( &config, "server.port", &ival ) ) printf( "server.port=%d\n", ival );
	if( config_lookup_string( &config, "server.name", &sval ) ) printf( "server.name=%s\n", sval );
	if( config_lookup_float( &config, "server.rate", &dval ) ) printf( "server.rate=%.2f\n", dval );
	if( config_lookup_bool( &config, "server.pvp", &bval ) ) printf( "server.pvp=%d\n", bval );

	config_setting_t* list = config_lookup( &config, "server.maps" );

	if( list != nullptr ){
		printf( "server.maps(%d):", config_setting_length( list ) );

		for( int i = 0; i < config_setting_length( list ); i++ ){
			printf( " %s", config_setting_get_string_elem( list, i ) );
		}

		printf( "\n" );
	}

	config_destroy( &config );
	return 0;
}

class KatDb : public YamlDatabase {
public:
	KatDb() : YamlDatabase( "KAT_DB", 1 ) {}

	void clear() override {}
	const std::string getDefaultLocation() override { return ""; }

	uint64 parseBodyNode( const ryml::NodeRef& node ) override {
		uint32 id;
		std::string name;
		uint16 rate;
		bool flag;
		double ratio;

		if( !this->asUInt32( node, "Id", id ) ){
			printf( "entry: Id invalid\n" );
			return 0;
		}

		printf( "entry %u:", id );

		if( this->asString( node, "Name", name ) ) printf( " Name=%s", name.c_str() ); else printf( " Name=ERR" );
		if( this->asUInt16Rate( node, "Rate", rate ) ) printf( " Rate=%u", rate ); else printf( " Rate=ERR" );
		if( this->asBool( node, "Flag", flag ) ) printf( " Flag=%d", flag ); else printf( " Flag=ERR" );
		if( this->asDouble( node, "Ratio", ratio ) ) printf( " Ratio=%.3f", ratio ); else printf( " Ratio=ERR" );

		printf( "\n" );
		return 1;
	}

	int run( const std::string& text ) {
		this->parser = {};
		ryml::Tree tree;

		try{
			tree = this->parser.parse_in_arena( c4::to_csubstr( "kat.yml" ), c4::to_csubstr( text ) );
		}catch( const std::runtime_error& e ){
			printf( "yaml syntax error\n" );
			return 0;
		}

		uint64 count = 0;

		if( this->nodeExists( tree.rootref(), "Body" ) ){
			for( const ryml::NodeRef& node : tree["Body"] ){
				count += this->parseBodyNode( node );
			}
		}

		printf( "parsed %llu entries\n", (unsigned long long)count );
		return 0;
	}
};

int main( int argc, char** argv ) {
	if( argc != 3 ){
		fprintf( stderr, "usage: %s sv|unescape|conf|yaml <file>\n", argv[0] );
		return 2;
	}

	msg_silent = 0x3f;
	do_init_database();

	std::string text = slurp( argv[2] );

	if( !strcmp( argv[1], "sv" ) ) return run_sv( text );
	if( !strcmp( argv[1], "unescape" ) ) return run_unescape( text );
	if( !strcmp( argv[1], "conf" ) ) return run_conf( text );
	if( !strcmp( argv[1], "yaml" ) ) return KatDb().run( text );

	fprintf( stderr, "unknown mode %s\n", argv[1] );
	return 2;
}
