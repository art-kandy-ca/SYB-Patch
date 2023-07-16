#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <chrono>
#include <algorithm>
#include <assert.h>

namespace
{
    struct SYBFileInfo
    {
        std::string fileName{};
        std::string extension{};
        unsigned int fileSize = 0;
    };
    enum class SYBFileInfoField { FileName, FileSize };
}

int UnpackFiles( const char* inputPath, const char* outputPath )
{
    // Input file check
    if( !std::filesystem::exists( inputPath ) )
    {
        std::cout << "Error: Specified input SYB-file wasn't found!" << std::endl;
        return 1;
    }
    else
    {
        std::filesystem::path extention = std::filesystem::path( inputPath ).extension();
        if( extention.compare( ".syb" ) != 0 && extention.compare( ".SYB" ) != 0 )
        {
            std::cout << "Error: Specified input path is not a SYB-file!" << std::endl;
            return 1;
        }
    }

    std::ifstream inputFile( inputPath, std::ios_base::binary );


    // === Header section ===

    char buf[4];
    inputFile.read( buf, 4 );
    if( buf[0] != 0x56 || buf[1] != 0x58 || buf[2] != 0x42 || buf[3] != 0x47 )
    {
        std::cout << "Error: Specified input file has a wrong format!" << std::endl;
        return 1;
    }

    inputFile.read( buf, 4 );
    unsigned int fileinfoSize = buf[0] | buf[1] << 8 | buf[2] << 16 | buf[3] << 24;


    // === File info section ===

    std::vector<SYBFileInfo> fileInfos;

    const unsigned int cMaxFileNameSize = 128;
    char curFileName[cMaxFileNameSize];
    unsigned int curFileSize = 0;
    unsigned char curIdx = 0;
    SYBFileInfoField curFIField = SYBFileInfoField::FileName;

    for( unsigned int i = 0; i < fileinfoSize; ++i )
    {
        inputFile.read( buf, 1 );
        if( curFIField == SYBFileInfoField::FileName )
        {
            if( buf[0] != 0x00 )
            {
                curFileName[curIdx++] = buf[0];
                assert( curIdx < cMaxFileNameSize );
            }
            else // end of file name
            {
                curFileName[curIdx] = '\0';
                curIdx = 0;
                curFIField = SYBFileInfoField::FileSize;
            }
        }
        else
        {
            assert( curFIField == SYBFileInfoField::FileSize );

            curFileSize |= ( (unsigned char)buf[0] << ( 8 * curIdx++ ) );

            if( curIdx == 4 )
            {
                curIdx = 0;
                curFIField = SYBFileInfoField::FileName;
                fileInfos.push_back( SYBFileInfo{ curFileName, "", curFileSize});
                curFileSize = 0;
            }
        }
    }


    // === Files extraction ===

    // Output directory preparation
    if( !std::filesystem::exists( outputPath ) )
    {
        if( !std::filesystem::create_directory( outputPath ) )
        {
            std::cout << "Error: Couldn't create an output directory!" << std::endl;
            return 1;
        }
    }
    else
    {
        if( !std::filesystem::is_directory( outputPath ) )
        {
            std::cout << "Error: Output path should be directory!" << std::endl;
            return 1;
        }
    }

    for( const SYBFileInfo& curFI : fileInfos )
    {
        //std::cout << "Filename: " << curFI.fileName << "\t Filesize: " << curFI.fileSize << std::endl;

        std::ofstream outputFile( std::string( outputPath ) + "\\" + curFI.fileName, std::ios_base::binary );

        const unsigned int cBufSize = 10 * 1024;
        char buffer[cBufSize];

        unsigned int leftToRead = curFI.fileSize;
        do
        {
            unsigned int readSize = std::min<unsigned int>( leftToRead, cBufSize );

            inputFile.read( buffer, readSize );
            outputFile.write( buffer, readSize );

            leftToRead -= readSize;
        } while( leftToRead > 0 );
    }
    std::cout << fileInfos.size() << " files successfully unpacked." << std::endl;

    return 0;
}

int PackFiles( const char* inputPath, const char* outputPath )
{
    // Input directory check
    if( !std::filesystem::exists( inputPath ) )
    {
        std::cout << "Error: Specified input directory wasn't found!" << std::endl;
        return 1;
    }
    else
    {
        if( !std::filesystem::is_directory( inputPath ) )
        {
            std::cout << "Error: Specified input path is not a directory!" << std::endl;
            return 1;
        }
    }
    // Output file check
    if( std::filesystem::exists( outputPath ) )
    {
        if( std::filesystem::is_directory( outputPath ) )
        {
            std::cout << "Error: Specified output file is a directory!" << std::endl;
            return 1;
        }
        std::cout << "Warning: Specified output file exists! It will be rewritten!" << std::endl;
    }

    std::ofstream outputFile( outputPath, std::ios_base::binary );


    // === File info collection ===

    std::vector<SYBFileInfo> fileInfos;
    unsigned int fileInfoSectionSize = 0;
    for( const auto& entry : std::filesystem::directory_iterator( inputPath ) )
    {
        //std::cout << entry.path() << std::endl;
        fileInfos.push_back( SYBFileInfo{ entry.path().filename().string(), entry.path().extension().string(), (unsigned int)entry.file_size()});

        fileInfoSectionSize += (unsigned int)fileInfos.back().fileName.length() + 1u + 4u;
    }

    // Files sorting according to original order
    std::stable_sort( fileInfos.begin(), fileInfos.end(),
                        []( const SYBFileInfo& fi1, const SYBFileInfo& fi2 )
                        {
                            return fi1.extension == fi2.extension &&                                   
                                std::lexicographical_compare(fi1.fileName.begin(), fi1.fileName.end(),
                                                            fi1.fileName.begin(), fi1.fileName.end(),
                                                            [](char a, char b) { return a != '_' && b == '_' || a < b; }) ||
                                fi1.extension != fi2.extension &&
                                  ( fi1.extension == ".mp3" ||
                                    fi1.extension == ".wav" && fi2.extension != ".mp3" ||
                                    fi1.extension == ".jpg" && fi2.extension != ".mp3" && fi2.extension != ".wav" );
                        } );


    // === Header section ===

    outputFile.put( 0x56 );
    outputFile.put( 0x58 );
    outputFile.put( 0x42 );
    outputFile.put( 0x47 );
    outputFile.put( ( fileInfoSectionSize >> 0 ) & 0xFF );
    outputFile.put( ( fileInfoSectionSize >> 8 ) & 0xFF );
    outputFile.put( ( fileInfoSectionSize >> 16 ) & 0xFF );
    outputFile.put( ( fileInfoSectionSize >> 24 ) & 0xFF );


    // === File info section ===

    for( const SYBFileInfo& curFI : fileInfos )
    {
        //std::cout << "Filename: " << curFI.fileName << "\t Filesize: " << curFI.fileSize << std::endl;

        outputFile.write( curFI.fileName.c_str(), curFI.fileName.length() + 1 );
        outputFile.put( ( curFI.fileSize >> 0 ) & 0xFF );
        outputFile.put( ( curFI.fileSize >> 8 ) & 0xFF );
        outputFile.put( ( curFI.fileSize >> 16 ) & 0xFF );
        outputFile.put( ( curFI.fileSize >> 24 ) & 0xFF );
    }


    // === File content section ===

    for( const SYBFileInfo& curFI : fileInfos )
    {
        std::ifstream inputFile( std::string( inputPath ) + "\\" + curFI.fileName, std::ios_base::binary );

        const unsigned int cBufSize = 10 * 1024;
        char buffer[cBufSize];

        unsigned int leftToRead = curFI.fileSize;
        do
        {
            unsigned int readSize = std::min<unsigned int>( leftToRead, cBufSize );

            inputFile.read( buffer, readSize );
            outputFile.write( buffer, readSize );

            leftToRead -= readSize;
        } while( leftToRead > 0 );
    }
    std::cout << fileInfos.size() << " files successfully packed." << std::endl;

    return 0;
}

int main(char argc, char* argv[])
{
#ifndef NDEBUG
    auto timeBegin = std::chrono::high_resolution_clock::now();
#endif

    std::cout << "Welcome to SYB-Patch program.\n"
        "Its main purpose is to pack/unpack SYB-files from \"Syberia 2\" game.\n"
        "Created for Ukrainian version by Art-Kandy, June 2023" << std::endl;

    if( argc != 4 )
    {
        std::cout << "The program should be run with the following arguments:\n" << std::endl;
        std::cout << "SYBPatch [mode] [inputPath] [outputPath]\n" << std::endl;
        std::cout << "where [mode] = unpack or pack" << std::endl;
        std::cout << "      [inputPath] = a source SYB-file to unpack (unpack-mode) or a source directory to pack (pack-mode)" << std::endl;
        std::cout << "      [outputPath] = a target directory for unpacking (unpack-mode) or a result SYB-file for packing (pack-mode)" << std::endl;

        return 0;
    }

    const char* mode = argv[1];
    const char* inputPath = argv[2];
    const char* outputPath = argv[3];

    int retCode = 0;
    if( strcmp( mode, "unpack" ) == 0 )
    {
        retCode = UnpackFiles( inputPath, outputPath );
    }
    else if( strcmp( mode, "pack" ) == 0 )
    {
        retCode = PackFiles( inputPath, outputPath );
    }

#ifndef NDEBUG
    auto timeEnd = std::chrono::high_resolution_clock::now();
    std::cout << "Overall time spent (s): " << std::chrono::duration_cast<std::chrono::milliseconds>( timeEnd - timeBegin ).count() / 1000.f << std::endl;
#endif

    return retCode;
}