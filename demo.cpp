#include "mos6502.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <istream>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>


template <typename T>
T swizzle(T value)
{
    static_assert(std::is_trivially_copyable<T>::value, "swizzle requires trivially copyable types");

    uint8_t bytes[sizeof(T)];
    std::memcpy(bytes, &value, sizeof(T));
    std::reverse(bytes, bytes + sizeof(T));

    T result;
    std::memcpy(&result, bytes, sizeof(T));
    return result;
}

struct SidFile
{
    struct Header 
    {
        char        _magic[4];              // PSID (simple) or RSID (advanced, requires full C64 emulation).

        uint16_t    _version;               // Available version number can be 0001, 0002, 0003 or 0004. Headers of version 2,
                                            // 3 and 4 provide additional fields. RSID and PSID v2NG files must have 0002,
                                            // 0003 or 0004 here.

        uint16_t    _dataOffset;            // Offset from start of file to the C64 binary data area.
                                            // Because of the fixed size of the header, this is either 0x0076 for version 1
                                            // and 0x007C for version 2, 3 and 4.

        uint16_t    _loadAddress;           // The C64 memory location where to put the C64 data. 0 means the data are in
                                            // original C64 binary file format, i.e. the first two bytes of the data contain
                                            // the little-endian load address (low byte, high byte). This must always be true
                                            // for RSID files. Furthermore, the actual load address must NOT be less than
                                            // $07E8 in RSID files.
                                            // https://gist.github.com/cbmeeks/2b107f0a8d36fc461ebb056e94b2f4d6#file-sid-txt-L104

        uint16_t    _initAddress;           // The start address of the machine code subroutine that initializes a song,
                                            // accepting the contents of the 8-bit 6510 Accumulator as the song number
                                            // parameter. 0 means the address is equal to the effective load address.
                                            // In RSID files initAddress must never point to a ROM area ($A000-$BFFF or
                                            // $D000-$FFFF) or be lower than $07E8. Also, if the C64 BASIC flag is set,
                                            // initAddress must be 0.

        uint16_t    _playAddress;           // The start address of the machine code subroutine that can be called frequently
                                            // to produce a continuous sound. 0 means the initialization subroutine is
                                            // expected to install an interrupt handler, which then calls the music player at
                                            // some place. This must always be true for RSID files.

        uint16_t    _songs;                 // The number of songs (or sound effects) that can be initialized by calling the
                                            // init address. The minimum is 1. The maximum is 256.

        uint16_t    _startSong;             // The song number to be played by default. This value is optional. It often
                                            // specifies the first song you would hear upon starting the program is has been
                                            // taken from. It has a default of 1.

        uint32_t    _speed;                 // https://gist.github.com/cbmeeks/2b107f0a8d36fc461ebb056e94b2f4d6#file-sid-txt-L157

        char        _name[32];
        char        _author[32];
        char        _released[32];
    };

    bool load( const std::string& file )
    {
        std::ifstream stream( file, std::ios::binary | std::ios::ate );

        if ( !stream )
        {
            return false;
        }

        _size = stream.tellg();
        _data = std::make_unique<std::uint8_t[]>( _size );

        stream.seekg( 0, std::ios::beg );
        stream.read( reinterpret_cast<char*>( _data.get()), _size );
        return true;
    }

    Header getHeader() const
    {
        auto header = *reinterpret_cast <const Header*> ( _data.get() );

        header._version = swizzle( header._version );
        header._dataOffset = swizzle( header._dataOffset );
        header._loadAddress = swizzle( header._loadAddress );
        header._initAddress = swizzle( header._initAddress );
        header._playAddress = swizzle( header._playAddress );
        header._songs = swizzle( header._songs );
        header._startSong = swizzle( header._startSong );
        header._speed = swizzle( header._speed );

        return header;
    }

    const uint8_t* getData() const
    {
        return _data.get() + getHeader()._dataOffset;
    }

    uint16_t getDataSize() const
    {
        return _size - getHeader()._dataOffset;
    }

    uint16_t getLoadAddress() const
    {
        auto header = getHeader();
        auto address = header._loadAddress;

        const auto *data = getData();

        if ( address == 0 )
        {
            // Get the first word from the data, which for the C64 is typically the load address.

            auto value = *reinterpret_cast <const uint16_t*> ( data );
            address = value;
        }

        return address;
    }

    std::size_t _size = 0;
    std::unique_ptr <std::uint8_t[]> _data;
};

struct Computer
{
    uint8_t _memory[1<<16] = { 0 };
    
    /// Copies into memory.
    void copy( const uint8_t *data, size_t size, uint16_t address )
    {
        std::memcpy( _memory + address, data + 2, size - 2 );
    }
};

static Computer _computer;

static uint8_t read_mem(uint16_t addr) 
{
    return _computer._memory[addr];
}

static void write_mem(uint16_t addr, uint8_t value) 
{
    if ( addr >= 0xD400 && addr < 0xD41C )
    {
        std::printf("SID write: addr=0x%04X value=0x%02X\n", addr, value);
    }

    _computer._memory[addr] = value;
}

int main(int argc, char** argv) 
{
    SidFile sid;
    sid.load( "../a.sid" );

    auto header = sid.getHeader();

    // reset vector is set to 0x8000
    
    _computer._memory[0xFFFC] = 0x00;   // reset vector low byte
    _computer._memory[0xFFFD] = 0x80;   // reset vector high byte
    
    mos6502 cpu( read_mem, write_mem );
    
    // Where in memory should the SID payload be loaded?

    auto address = sid.getLoadAddress();

    // Copy the SID program and data into the computer's memory at the load address.

    _computer.copy( sid.getData(), sid.getDataSize(), address );
    cpu.Reset();

    _computer._memory[0x8000] = 0x20; // JSR
    _computer._memory[0x8001] = header._initAddress & 0xFF;         // low byte of init address
    _computer._memory[0x8002] = header._initAddress >> 8;           // high byte of init address

    _computer._memory[0x8003] = 0x20; // JSR
    _computer._memory[0x8004] = header._playAddress & 0xFF;         // low byte of init address
    _computer._memory[0x8005] = header._playAddress >> 8;           // high byte of init address

    _computer._memory[0x8006] = 0x4c; // JMP to address 0x8003 forever.
    _computer._memory[0x8007] = 0x03;
    _computer._memory[0x8008] = 0x80;

    uint64_t cycleCount = 0;

    auto updateTime = std::chrono::high_resolution_clock::now();

    while ( true )
    {
        cpu.Run( 1, cycleCount );
    
        // std::printf("PC=0x%04X A=0x%02X X=0x%02X Y=0x%02X cycles=%llu\n",
        //         cpu.GetPC(), cpu.GetA(), cpu.GetX(), cpu.GetY(),
        //         static_cast<unsigned long long>(cycle_count));

        if ( cpu.GetPC() == 0x8000 )
        {
            updateTime = std::chrono::high_resolution_clock::now();
            printf( "Execute init()\n" );
        } 
        else if ( cpu.GetPC() == 0x8003 )
        {
            updateTime = std::chrono::high_resolution_clock::now();
            printf( "Execute play()\n" );
        }
        else 
        {
            continue;
        }

        // Dumb timing to make it to 60Hz for now.
        std::this_thread::sleep_for( std::chrono::milliseconds( 16 ) );
    }

    return 0;
}
