/* ---------------------------------------------------------------------
 * Numenta Platform for Intelligent Computing (NuPIC)
 * Copyright (C) 2013, Numenta, Inc.  Unless you have an agreement
 * with Numenta, Inc., for a separate license for this software code, the
 * following terms and conditions apply:
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Affero Public License for more details.
 *
 * You should have received a copy of the GNU Affero Public License
 * along with this program.  If not, see http://www.gnu.org/licenses.
 *
 * http://numenta.org/licenses/
 * ---------------------------------------------------------------------
 */

/** @file 
 * Definitions for the FStream classes
 * 
 * These classes are versions of ifstream and ofstream that accept platform independent
 * (i.e. windows or unix) utf-8 path specifiers for their constructor and open() methods.
 *
 * The native ifstream and ofstream classes on unix already accept UTF-8, but on windows,
 * we must convert the utf-8 path to unicode and then pass it to the 'w' version of
 * ifstream or ofstream
 */


#include "nupic/utils/Log.hpp"
#include "nupic/os/Path.hpp"
#include "nupic/os/FStream.hpp"
#include "nupic/os/Directory.hpp"
#include "nupic/os/Env.hpp"
#include <fstream>
#include <cstdlib>
#include <boost/iostreams/filter/zlib.hpp>

//#include <zlib.h>


using namespace nupic;



//////////////////////////////////////////////////////////////////////////
/// open the given file by name
/////////////////////////////////////////////////////////////////////////
void IFStream::open(const char * filename, ios_base::openmode mode)
{
  std::ifstream::open(filename, mode);
}
    
  
//////////////////////////////////////////////////////////////////////////
/// open the given file by name
/////////////////////////////////////////////////////////////////////////
void OFStream::open(const char * filename, ios_base::openmode mode)
{
  std::ofstream::open(filename, mode);
}
 
//////////////////////////////////////////////////////////////////////////
/// open a ZLIB file by name
/////////////////////////////////////////////////////////////////////////

void *ZLib::fopen(const std::string &filename, const std::string &mode,
  std::string *errorMessage)
{
  throw std::invalid_argument("Not implemented.");
    if(mode.empty()) throw std::invalid_argument("Mode may not be empty.");

//  gzFile fs = nullptr;
//  { // zlib may not be thread-safe in its current compiled state.
//    int attempts = 0;
//    const int maxAttempts = 1;
//    int lastError = 0;
//    while(1) {
//      fs = gzopen(filename.c_str(), mode.c_str());
//     if(fs) break;
//
//      int error = errno;
//      if(error != lastError) {
//        std::string message("Unknown error.");
//        // lastError = error;
//        switch(error) {
//          case Z_STREAM_ERROR: message = "Zlib stream error."; break;
//          case Z_DATA_ERROR: message = "Zlib data error."; break;
//          case Z_MEM_ERROR: message = "Zlib memory error."; break;
//          case Z_BUF_ERROR: message = "Zlib buffer error."; break;
//          case Z_VERSION_ERROR: message = "Zlib version error."; break;
//          default: message = ::strerror(error); break;
//        }   
//        if(errorMessage) {
//          *errorMessage = message;
//        }
//        else if(maxAttempts > 1) { // If we will try again, warn about failure.
//          std::cerr << "Warning: Failed to open file '" 
//               << filename << "': " << message << std::endl;
//        }
//      }   
//      if((++attempts) >= maxAttempts) break;
//      ::usleep(10000);
//    }   
//  }
//#endif
//  return fs;
}




