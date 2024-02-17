#pragma once

#include <compressonator.h>
#include <filesystem>
#include <list>

class std::ifstream;
class std::ofstream;

enum PlatForm
{
    PC = 12,
    XBOX360 = 11,
    PS3 = 10,
    UNKNOWN_PLAT_FORM = 0
};

enum KleiPixelFormat
{
    DXT1 = 0,
	DXT3 = 1,
	DXT5 = 2,
    ARGB = 4,
    RGB = 5,
    ETC2_EAC = 18,
    UNKNOWN_PIXEL_FORMAT = 7
};

enum KleiTextureType
{
    ONE_D = 1,
    TWO_D = 2,
    THREE_D = 3,
    CUBE_MAP = 4
};

static constexpr std::string_view KTEX_HEADER = "KTEX";


struct KtexHeader
{
    uint32_t platform;
    uint32_t pixel_format;
    uint32_t texture_type;
    uint32_t num_mips;
    uint32_t flags;
    uint32_t remainder;
    uint32_t old_remainder;
    uint32_t origin;
    KtexHeader(): platform(0), pixel_format(0), texture_type(0), num_mips(0), flags(0), remainder(0), old_remainder(0),
	origin(0)
    {
	    
    }
};

struct MipMap
{
    uint16_t width;
    uint16_t height;
    uint16_t pitch;
    uint32_t size;
    uint8_t* data;
    MipMap(): width(0), height(0), pitch(0), size(0), data(nullptr)
    {
    }
    ~MipMap()
    {
        delete[] data;
    }
};

class TexFile
{
public:
    TexFile(std::filesystem::path path, char* pBuf, size_t bufSize);
    ~TexFile();
public:
    bool Load();
    bool IsPreCaveUpdate() const;
    bool Convert(const std::string& savePath, KleiPixelFormat pf = ARGB) const;
    void SaveToPng(const std::string& dir) const;
private:
    bool ReadHeader(std::ifstream& stream);
    bool ReadMipMaps(std::ifstream& stream);
    void SaveOneMipMapToPng(std::string path, const MipMap& mipMap) const;
    void SaveHeader(std::ofstream& stream, KleiPixelFormat pf = ARGB) const;
    std::optional<CMP_FORMAT> GetThirdPixelFormat(uint32_t pixelFormat) const;
private:
    KtexHeader m_header;
    std::list<MipMap> m_mip_maps;
    std::filesystem::path m_path;
    CMP_FORMAT m_third_pixel_format;
    char* m_buf;
    size_t m_buf_size;
};
