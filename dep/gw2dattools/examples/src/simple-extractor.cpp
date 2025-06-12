#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <fstream>

#include <gw2dattools/interface/ANDatInterface.h>
#include <gw2dattools/compression/inflateDatFileBuffer.h>

int main( int argc, char* argv[] ) {
    const uint32_t aBufferSize = 1024 * 1024 * 30; // We make the assumption that no file is bigger than 30 Mb

    auto datfile = "Gw2.dat";

    auto pANDatInterface = gw2dt::datfile::createANDatInterface( datfile );
    auto aFileRecordVect = pANDatInterface->getFileRecordVect( );

    uint8_t* pOriBuffer = new uint8_t[aBufferSize];
    uint8_t* pInfBuffer = new uint8_t[aBufferSize];

    for ( auto& it : aFileRecordVect ) {
        uint32_t aOriSize = aBufferSize;
        pANDatInterface->getBuffer( it, aOriSize, pOriBuffer );

        std::cout << "Processing File " << it.fileId << std::endl;

        std::ofstream aOFStream;
        std::ostringstream oss;
        oss << "/tmp/" << it.fileId;
        aOFStream.open( oss.str( ).c_str( ), std::ios::binary | std::ios::out );

        if ( aOriSize == aBufferSize ) {
            std::cout << "File " << it.fileId << " has a size greater than (or equal to) 30Mb." << std::endl;
        }

        if ( it.isCompressed ) {
            uint32_t aInfSize = aBufferSize;

            try {
                gw2dt::compression::inflateDatFileBuffer( aOriSize, pOriBuffer, aInfSize, pInfBuffer );
                aOFStream.write( reinterpret_cast<const char*>( pInfBuffer ), aInfSize );
            } catch ( std::exception& iException ) {
                std::cout << "File " << it.fileId << " failed to decompress: " << std::string( iException.what( ) ) << std::endl;
            }
        } else {

            // skip 4 byte every 65532 byte here

            //aOFStream.write(reinterpret_cast<const char*>(pOriBuffer), aOriSize);
        }

        aOFStream.close( );
    }

    delete[] pOriBuffer;
    delete[] pInfBuffer;

    return 0;
};
