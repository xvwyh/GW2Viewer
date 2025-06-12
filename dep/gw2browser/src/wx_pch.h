/** \file       wx_pch.h
 *  \brief      Header to create Pre-Compiled Header (PCH).
 *  \author     Rhoot
 */

/**
 * Copyright (C) 2014-2019 Khralkatorrix <https://github.com/kytulendu>
 * Copyright (C) 2012 Rhoot <https://github.com/rhoot>
 *
 * This file is part of Gw2Browser.
 *
 * Gw2Browser is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef WX_PCH_H_INCLUDED
#define WX_PCH_H_INCLUDED

// gw2dattools
#include <gw2dattools/compression/inflateDatFileBuffer.h>

namespace gw2b
{
    typedef int8_t                  int8;       /**< Signed 8-bit integer. */
    typedef int16_t                 int16;      /**< Signed 16-bit integer. */
    typedef int32_t                 int32;      /**< Signed 32-bit integer. */
    typedef int64_t                 int64;      /**< Signed 64-bit integer. */

    typedef uint8_t                 uint8;      /**< Unsigned 8-bit integer. */
    typedef uint16_t                uint16;     /**< Unsigned 16-bit integer. */
    typedef uint32_t                uint32;     /**< Unsigned 32-bit integer. */
    typedef uint64_t                uint64;     /**< Unsigned 64-bit integer. */

    typedef uint8                   byte;       /**< Unsigned byte. */
    typedef int8                    sbyte;      /**< Signed byte. */
    typedef char                    char8;      /**< 8-bit character. */

    typedef unsigned short          ushort;     /**< Short for 'unsigned short'. */
    typedef unsigned int            uint;       /**< Short for 'unsigned int'. */
    typedef unsigned long           ulong;      /**< Short for 'unsigned long'. */

    typedef float                   float32;    /**< 32-bit IEEE floating point number. */
    typedef double                  float64;    /**< 64-bit IEEE floating point number. */

}; // namespace gw2b

// Gw2Browser includes
#include "Util/Misc.h"

#endif // WX_PCH_H_INCLUDED
