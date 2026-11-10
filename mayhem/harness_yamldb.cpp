// Fuzzes rAthena's YamlDatabase (ryml parse + typed conversions) on in-memory input.
// YamlDatabase::load() reads a file path and then follows Footer/Imports to further
// arbitrary paths; here the buffer is parsed with the same parser call and the protected
// conversion helpers walk the tree, so no file I/O or import recursion happens.
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <string>

#include <common/database.hpp>
#include <common/showmsg.hpp>

extern void do_init_database();

class FuzzYamlDatabase : public YamlDatabase {
public:
	FuzzYamlDatabase() : YamlDatabase( "FUZZ_DB", 1 ) {}

	void clear() override {}
	const std::string getDefaultLocation() override { return "fuzz.yml"; }
	uint64 parseBodyNode( const ryml::NodeRef& node ) override { return this->walk( node, 0 ); }

	bool run( const char* buf, size_t len ) {
		this->parser = {};
		ryml::Tree tree;

		try{
			tree = this->parser.parse_in_arena( c4::to_csubstr( "fuzz.yml" ), c4::csubstr( buf, len ) );
		}catch( const std::runtime_error& ){
			return false;
		}

		try{
			ryml::NodeRef root = tree.rootref();

			if( !this->nodeExists( root, "Header" ) ){
				return false;
			}

			ryml::NodeRef header = tree["Header"];
			std::string type;
			uint16 version;

			this->asString( header, "Type", type );
			this->asUInt16( header, "Version", version );

			if( this->nodeExists( header, "Clear" ) ){
				bool clear;
				this->asBool( header, "Clear", clear );
			}

			if( this->nodeExists( root, "Body" ) ){
				for( const ryml::NodeRef& node : tree["Body"] ){
					this->parseBodyNode( node );
				}
			}

			if( this->nodeExists( root, "Footer" ) && this->nodeExists( tree["Footer"], "Imports" ) ){
				for( const ryml::NodeRef& node : tree["Footer"]["Imports"] ){
					std::string path, mode;
					bool generator;

					this->asString( node, "Path", path );
					this->asString( node, "Mode", mode );
					this->asBool( node, "Generator", generator );
				}
			}
		}catch( const std::runtime_error& ){
			return false;
		}

		return true;
	}

private:
	uint64 walk( const ryml::NodeRef& node, int depth ) {
		uint64 count = 0;

		if( depth > 32 || !node.is_map() ){
			return count;
		}

		for( const ryml::NodeRef& child : node.children() ){
			if( !child.has_key() ){
				continue;
			}

			std::string name( child.key().str, child.key().len );

			std::string s;
			bool b;
			int16 i16;
			uint16 u16, rate16;
			int32 i32;
			uint32 u32, rate32;
			int64 i64;
			uint64 u64;
			float f;
			double d;

			this->asString( node, name, s );
			this->asBool( node, name, b );
			this->asInt16( node, name, i16 );
			this->asUInt16( node, name, u16 );
			this->asInt32( node, name, i32 );
			this->asUInt32( node, name, u32 );
			this->asInt64( node, name, i64 );
			this->asUInt64( node, name, u64 );
			this->asFloat( node, name, f );
			this->asDouble( node, name, d );
			this->asUInt16Rate( node, name, rate16 );
			this->asUInt32Rate( node, name, rate32 );
			this->getLineNumber( child );
			this->getColumnNumber( child );
			count++;

			if( child.is_map() ){
				count += this->walk( child, depth + 1 );
			}else if( child.is_seq() ){
				for( const ryml::NodeRef& elem : child.children() ){
					count += this->walk( elem, depth + 1 );
				}
			}
		}

		return count;
	}

};

static FuzzYamlDatabase* g_db;

extern "C" int LLVMFuzzerInitialize( int*, char*** ) {
	msg_silent = 0x3f;
	do_init_database();
	g_db = new FuzzYamlDatabase();
	return 0;
}

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size ) {
	std::string buf( reinterpret_cast<const char*>( data ), size );

	return g_db->run( buf.c_str(), buf.size() ) ? 0 : -1;
}
