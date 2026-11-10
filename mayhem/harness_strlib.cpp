// Fuzzes rAthena's delimiter-separated-value parsers (sv_parse/sv_split) and C-escape helpers (strlib.cpp),
// which read every db/*.txt and conf line. The first input byte picks delimiter and options.
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <common/showmsg.hpp>
#include <common/strlib.hpp>

extern "C" int LLVMFuzzerInitialize( int*, char*** ) {
	msg_silent = 0x3f;
	return 0;
}

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size ) {
	if( size < 3 ){
		return -1;
	}

	static const char delims[] = { ',', '\t', ':', '|', ' ', ';', '#', '"' };
	const char delim = delims[data[0] & 7];
	const int32 opt = ( data[1] & 1 ) | ( ( data[1] & 0x0e ) ) | ( ( data[1] & 0x10 ) );
	const size_t nfields = 1 + ( data[2] & 0x1f );
	const char* text = reinterpret_cast<const char*>( data + 3 );
	const size_t len = size - 3;

	// sv_parse: positions only, on a read-only NUL-terminated copy
	{
		std::string copy( text, len );
		std::vector<size_t> pos( 2 * ( nfields + 1 ) );
		bool error = false;
		size_t off = 0;

		for( int line = 0; line < 64 && off < len; line++ ){
			size_t n = sv_parse( copy.c_str(), len, off, delim, pos.data(), pos.size(), opt, error );

			if( error || n == (size_t)-1 || pos[1] <= off ){
				break;
			}

			off = pos[1];
		}
	}

	// sv_split: modifies the buffer in place
	{
		std::string copy( text, len );
		std::vector<char*> fields( nfields + 1 );
		bool error = false;
		size_t off = 0;

		for( int line = 0; line < 64 && off < len; line++ ){
			size_t n = sv_split( &copy[0], len, off, delim, fields.data(), fields.size(), opt, error );

			if( error || n == (size_t)-1 ){
				break;
			}

			char* next = fields[0];

			if( next == nullptr || next < copy.data() || next >= copy.data() + len || (size_t)( next - copy.data() ) <= off ){
				break;
			}

			off = next - copy.data();
		}
	}

	// sv_unescape_c may run in place; sv_escape_c needs len*4+1
	{
		std::string copy( text, len );
		std::string out( len + 1, '\0' );
		size_t n = sv_unescape_c( &out[0], copy.c_str(), len );

		std::string esc( n * 4 + 1, '\0' );
		sv_escape_c( &esc[0], out.c_str(), n, SV_ESCAPE_C_SUPPORTED );

		std::string inplace( text, len );
		sv_unescape_c( &inplace[0], inplace.c_str(), len );
	}

	skip_escaped_c( std::string( text, len ).c_str() );

	return 0;
}
