/** \file       FileReader.cpp
 *  \brief      Contains the definition for the base class of readers for the
 *              various file types.
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

#include "wx_pch.h"

#include "FileReader.h"

namespace gw2b {

    FileReader::FileReader( const std::vector<byte>& p_data, DatFile& p_datFile, ANetFileType p_fileType )
        : m_data( p_data )
        , m_datFile( p_datFile )
        , m_fileType( p_fileType ) {
    }

    FileReader::~FileReader() {
    }

    void FileReader::clean() {
        m_fileType = ANFT_Unknown;
    }

    byte const* FileReader::rawData( ) const {
        return m_data.data();
    }

}; // namespace gw2b
