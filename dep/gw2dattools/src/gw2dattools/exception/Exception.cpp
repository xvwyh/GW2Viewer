#include "gw2dattools/exception/Exception.h"

namespace gw2dt {
    namespace exception {

        Exception::Exception( const char* iReason ) :
            errorMessage( iReason ) {
        }

        Exception::~Exception( ) {
        }

        const char *Exception::what( ) const throw( ) {
            return errorMessage.c_str( );
        }

    }
}
