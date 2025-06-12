#include <iostream>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <fstream>

#include <gw2dattools/interface/ANDatInterface.h>
#include <gw2dattools/compression/inflateDatFileBuffer.h>
#include <gw2dattools/compression/inflateTextureFileBuffer.h>

int main( int argc, char* argv[] ) {
    if ( argc != 2 ) {
        std::cout << "usage: extract [file id]" << std::endl;
        return 0;
    }
    auto file_id = atoi( argv[1] );

    auto datfile = "Gw2.dat";

    std::cout << "Start" << std::endl;
    const uint32_t aBufferSize = 1024 * 1024 * 30; // We make the assumption that no file is bigger than 30 Mo

    auto pANDatInterface = gw2dt::datfile::createANDatInterface( datfile );

    std::cout << "Getting FileRecord Id: " << file_id << std::endl;
    auto aFileRecord = pANDatInterface->getFileRecordForFileId( file_id );

    uint8_t* pOriBuffer = new uint8_t[aBufferSize];
    uint8_t* pInfBuffer = new uint8_t[aBufferSize];
    uint8_t* pOutBuffer = new uint8_t[aBufferSize];

    uint8_t* pAtexBuffer = nullptr;
    uint32_t aAtexBufferSize = 0;

    uint32_t aOriSize = aBufferSize;
    pANDatInterface->getBuffer( aFileRecord, aOriSize, pOriBuffer );

    std::ostringstream aStringstream;
    aStringstream << "/tmp/";
    aStringstream << aFileRecord.fileId;

    std::ofstream aStream( aStringstream.str( ), std::ios::binary );

    if ( aOriSize == aBufferSize ) {
        std::cout << "File " << aFileRecord.fileId << " has a size greater than (or equal to) 30Mo." << std::endl;
    }

    if ( aFileRecord.isCompressed ) {
        uint32_t aInfSize = aBufferSize;
        std::cout << "File is compressed." << std::endl;

        try {
            gw2dt::compression::inflateDatFileBuffer( aOriSize, pOriBuffer, aInfSize, pInfBuffer );

            pAtexBuffer = pInfBuffer;
            aAtexBufferSize = aInfSize;
        } catch ( std::exception& iException ) {
            std::cout << "File " << aFileRecord.fileId << " failed to decompress: " << std::string( iException.what( ) ) << std::endl;
        }
    } else {
        std::cout << "File is not compressed." << std::endl;
        pAtexBuffer = pOriBuffer;
        aAtexBufferSize = aOriSize;
    }

    try {
        std::cout << "aAtexBufferSize: " << aAtexBufferSize << std::endl;
        uint32_t aOutSize = aBufferSize;
        gw2dt::compression::inflateTextureFileBuffer( aAtexBufferSize, pAtexBuffer, aOutSize, pOutBuffer );

        aStream.write( reinterpret_cast<char*>( pOutBuffer ), aOutSize );
    } catch ( std::exception& iException ) {
        std::cout << "Atex File " << aFileRecord.fileId << " failed to decompress: " << std::string( iException.what( ) ) << std::endl;
    }

    aStream.close( );

    delete[] pOriBuffer;
    delete[] pInfBuffer;

    return 0;
};
